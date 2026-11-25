// Node parameter services as a remote DDS peer reaches them: the fuzz input is the CDR body of a
// service request, deserialized with rclcpp's own Serialization<> and dispatched to the node's real
// service through the public ServiceBase::handle_request (parameter_service.cpp -> node_parameters).
// Byte 0 selects the service. Only rclcpp::exceptions::RCLError (send_response on a request id that
// no client sent) is tolerated; any other exception escaping a handler is a finding (cf. 2e5a8b9).
//
// State kept across inputs: one rclcpp context + node (a DDS participant costs milliseconds). The
// parameters an input can change are reset to their declared defaults after every input.
#include <rclcpp/rclcpp.hpp>
#include <rcl_interfaces/srv/describe_parameters.hpp>
#include <rcl_interfaces/srv/get_parameter_types.hpp>
#include <rcl_interfaces/srv/get_parameters.hpp>
#include <rcl_interfaces/srv/list_parameters.hpp>
#include <rcl_interfaces/srv/set_parameters.hpp>
#include <rcl_interfaces/srv/set_parameters_atomically.hpp>
#include <fastcdr/exceptions/Exception.h>
#include <cstring>
#include <vector>

static rclcpp::Node::SharedPtr node;
static std::vector<rclcpp::Parameter> defaults;

static rclcpp::ServiceBase::SharedPtr find(const std::string & suffix) {
  rclcpp::ServiceBase::SharedPtr out;
  node->get_node_base_interface()->for_each_callback_group([&](rclcpp::CallbackGroup::SharedPtr g) {
    g->collect_all_ptrs([](auto){}, [&](const rclcpp::ServiceBase::SharedPtr & s) {
      std::string n = s->get_service_name();
      if (n.size() >= suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0) out = s;
    }, [](auto){}, [](auto){}, [](auto){});
  });
  if (!out) abort();
  return out;
}

template<class Srv> static int run(const char * suffix, const uint8_t * d, size_t n) {
  using Req = typename Srv::Request;
  static auto svc = find(suffix);
  // CDR encapsulation header (little-endian CDR) as rmw puts it in front of every sample.
  rclcpp::SerializedMessage sm(n + 4);
  auto & m = sm.get_rcl_serialized_message();
  const uint8_t hdr[4] = {0, 1, 0, 0};
  memcpy(m.buffer, hdr, 4);
  if (n) memcpy(m.buffer + 4, d, n);
  m.buffer_length = n + 4;
  auto req = std::make_shared<Req>();
  try {
    rclcpp::Serialization<Req>().deserialize_message(&sm, req.get());
  } catch (const rclcpp::exceptions::RCLError &) {
    return -1;  // not a valid sample
  } catch (const eprosima::fastcdr::exception::Exception &) {
    // rmw_deserialize lets Fast CDR's exceptions escape; the DDS take path (rmw_take*) catches them
    // and drops the sample, so such bytes never reach rclcpp's code from the network.
    return -1;
  } catch (const std::bad_alloc &) {
    return -1;  // huge length prefix: same, the take path drops the sample
  } catch (const std::length_error &) {
    return -1;  // not a valid sample: rmw would drop it before it reaches the node
  }
  auto h = std::make_shared<rmw_request_id_t>();
  memset(h.get(), 0, sizeof(*h));
  try {
    svc->handle_request(h, req);
  } catch (const rclcpp::exceptions::RCLError &) {
  }
  return 0;
}

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  rclcpp::InitOptions io;
  io.auto_initialize_logging(false);
  rclcpp::init(0, nullptr, io, rclcpp::SignalHandlerOptions::None);
  node = std::make_shared<rclcpp::Node>("fuzz", "/ns");
  node->declare_parameter("a", 1);
  node->declare_parameter("s", "x");
  node->declare_parameter("b", true);
  node->declare_parameter("arr", std::vector<int64_t>{1, 2});
  rcl_interfaces::msg::ParameterDescriptor di;
  di.integer_range.resize(1);
  di.integer_range[0].from_value = -10; di.integer_range[0].to_value = 10; di.integer_range[0].step = 2;
  node->declare_parameter("i", 0, di);
  rcl_interfaces::msg::ParameterDescriptor dd;
  dd.floating_point_range.resize(1);
  dd.floating_point_range[0].from_value = 0.0; dd.floating_point_range[0].to_value = 1.0; dd.floating_point_range[0].step = 0.25;
  node->declare_parameter("d", 0.5, dd);
  rcl_interfaces::msg::ParameterDescriptor dr; dr.read_only = true;
  node->declare_parameter("ro", "fixed", dr);
  rcl_interfaces::msg::ParameterDescriptor dy; dy.dynamic_typing = true;
  node->declare_parameter("dyn", rclcpp::ParameterValue(3), dy);
  for (const char * p : {"a", "s", "b", "arr", "i", "d", "dyn", "use_sim_time"}) defaults.push_back(node->get_parameter(p));
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * d, size_t n) {
  if (n < 1) return -1;
  int r;
  switch (d[0] % 6) {
    case 0: r = run<rcl_interfaces::srv::SetParameters>("/set_parameters", d + 1, n - 1); break;
    case 1: r = run<rcl_interfaces::srv::SetParametersAtomically>("/set_parameters_atomically", d + 1, n - 1); break;
    case 2: r = run<rcl_interfaces::srv::GetParameters>("/get_parameters", d + 1, n - 1); break;
    case 3: r = run<rcl_interfaces::srv::GetParameterTypes>("/get_parameter_types", d + 1, n - 1); break;
    case 4: r = run<rcl_interfaces::srv::DescribeParameters>("/describe_parameters", d + 1, n - 1); break;
    default: r = run<rcl_interfaces::srv::ListParameters>("/list_parameters", d + 1, n - 1); break;
  }
  // Reset what a request may have changed ("dyn" may have been retyped, which set_parameters allows).
  node->set_parameters(defaults);
  return r;
}
