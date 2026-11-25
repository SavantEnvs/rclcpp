// rclcpp_components::ComponentManager's container services (what `ros2 component load` and any DDS
// peer send to a component container): the fuzz input is the CDR body of a LoadNode / UnloadNode /
// ListNodes request, deserialized with rclcpp's Serialization<> and dispatched to the manager's real
// service via the public ServiceBase::handle_request. LoadNode reaches get_component_resources
// (ament index parsing), create_node_options (parameters, remap rules, extra_arguments, log levels)
// and class_loader instantiation of the components installed in the image (tf2_ros,
// robot_state_publisher). Byte 0 selects the service.
//
// State kept across inputs: the rclcpp context, the manager node and its class loaders (a read-only
// library cache). Every component node an input loaded is unloaded again after the input, and
// rcutils' process-global logger-level map is reset: LoadNode.log_level reaches
// NodeOptions::log_level, and Node's constructor writes the node's logger level into that map, which
// unloading the node does not undo.
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/component_manager.hpp>
#include <composition_interfaces/srv/list_nodes.hpp>
#include <composition_interfaces/srv/load_node.hpp>
#include <composition_interfaces/srv/unload_node.hpp>
#include <fastcdr/exceptions/Exception.h>
#include <rcutils/logging.h>
#include <cstring>
#include <type_traits>
#include <vector>

namespace {
struct FuzzManager : rclcpp_components::ComponentManager {
  using ComponentManager::ComponentManager;
  void unload_all() {
    std::vector<uint64_t> ids;
    for (auto & kv : node_wrappers_) ids.push_back(kv.first);
    for (uint64_t id : ids) {
      auto req = std::make_shared<composition_interfaces::srv::UnloadNode::Request>();
      req->unique_id = id;
      auto res = std::make_shared<composition_interfaces::srv::UnloadNode::Response>();
      on_unload_node(nullptr, req, res);
    }
  }
};
}  // namespace

// Deliberately never destroyed: ~ComponentManager at process exit would unload class_loader
// libraries after class_loader's own statics are gone (a teardown-order crash of the harness, not of
// any input), so the manager and executor live until the process ends.
static std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> & exec =
  *new std::shared_ptr<rclcpp::executors::SingleThreadedExecutor>();
static std::shared_ptr<FuzzManager> & manager = *new std::shared_ptr<FuzzManager>();

static rclcpp::ServiceBase::SharedPtr find(const std::string & suffix) {
  rclcpp::ServiceBase::SharedPtr out;
  manager->get_node_base_interface()->for_each_callback_group([&](rclcpp::CallbackGroup::SharedPtr g) {
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
  rclcpp::SerializedMessage sm(n + 4);
  auto & m = sm.get_rcl_serialized_message();
  const uint8_t hdr[4] = {0, 1, 0, 0};  // CDR encapsulation header, as rmw prefixes every sample
  memcpy(m.buffer, hdr, 4);
  if (n) memcpy(m.buffer + 4, d, n);
  m.buffer_length = n + 4;
  auto req = std::make_shared<Req>();
  try {
    rclcpp::Serialization<Req>().deserialize_message(&sm, req.get());
  } catch (const rclcpp::exceptions::RCLError &) {
    return -1;  // not a valid sample
  } catch (const eprosima::fastcdr::exception::Exception &) {
    return -1;  // rmw_deserialize lets Fast CDR's exceptions escape; the DDS take path drops the sample
  } catch (const std::bad_alloc &) {
    return -1;
  } catch (const std::length_error &) {
    return -1;
  }
  if constexpr (std::is_same_v<Srv, composition_interfaces::srv::LoadNode>) {
    // rosbag2_transport's components (Player/Recorder) read and write bag directories in the cwd,
    // which would make one input's result depend on earlier ones; keep the container to the
    // side-effect-free components (tf2_ros, robot_state_publisher).
    if (req->package_name == "rosbag2_transport") return -1;
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
  exec = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  manager = std::make_shared<FuzzManager>(exec, "fuzz_container");
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * d, size_t n) {
  if (n < 1) return -1;
  int r;
  switch (d[0] % 3) {
    case 0: r = run<composition_interfaces::srv::LoadNode>("/_container/load_node", d + 1, n - 1); break;
    case 1: r = run<composition_interfaces::srv::UnloadNode>("/_container/unload_node", d + 1, n - 1); break;
    default: r = run<composition_interfaces::srv::ListNodes>("/_container/list_nodes", d + 1, n - 1); break;
  }
  manager->unload_all();
  // Drop every logger level an input set (rclcpp never configured rcl logging here, so this restores
  // exactly the state LLVMFuzzerInitialize left).
  if (rcutils_logging_shutdown() != RCUTILS_RET_OK || rcutils_logging_initialize() != RCUTILS_RET_OK) abort();
  return r;
}
