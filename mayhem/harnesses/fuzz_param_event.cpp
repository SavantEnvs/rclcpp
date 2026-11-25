// /parameter_events messages as any DDS peer may publish them: the fuzz input is the CDR body of an
// rcl_interfaces/msg/ParameterEvent, deserialized with rclcpp::Serialization and handed to the
// consumers rclcpp offers for it (ParameterEventsFilter, ParameterEventHandler's static helpers,
// Parameter::from_parameter_msg / to_string).
#include <rclcpp/parameter_event_handler.hpp>
#include <rclcpp/parameter_events_filter.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rcl_interfaces/msg/parameter_event.hpp>
#include <fastcdr/exceptions/Exception.h>
#include <cstring>

using rcl_interfaces::msg::ParameterEvent;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * d, size_t n) {
  rclcpp::SerializedMessage sm(n + 4);
  auto & m = sm.get_rcl_serialized_message();
  const uint8_t hdr[4] = {0, 1, 0, 0};
  memcpy(m.buffer, hdr, 4);
  if (n) memcpy(m.buffer + 4, d, n);
  m.buffer_length = n + 4;
  auto ev = std::make_shared<ParameterEvent>();
  rclcpp::Serialization<ParameterEvent> ser;
  try {
    ser.deserialize_message(&sm, ev.get());
  } catch (const rclcpp::exceptions::RCLError &) {
    return -1;  // not a valid sample
  } catch (const eprosima::fastcdr::exception::Exception &) {
    // rmw_deserialize lets Fast CDR's exceptions escape; the DDS take path (rmw_take*) catches them
    // and drops the sample, so such bytes never reach rclcpp's code from the network.
    return -1;
  } catch (const std::bad_alloc &) {
    return -1;  // huge length prefix: same, the take path drops the sample
  } catch (const std::length_error &) {
    return -1;
  }
  using ET = rclcpp::ParameterEventsFilter::EventType;
  rclcpp::ParameterEventsFilter filter(ev, {"a", "b", "s"}, {ET::NEW, ET::DELETED, ET::CHANGED});
  for (auto & p : filter.get_events()) (void)p.second->name;
  for (const auto & p : rclcpp::ParameterEventHandler::get_parameters_from_event(*ev)) {
    (void)std::to_string(p);
    (void)p.get_type_name();
  }
  try {
    (void)rclcpp::ParameterEventHandler::get_parameter_from_event(*ev, "a", ev->node);
  } catch (const std::runtime_error &) {  // documented: parameter not found in event
  }
  for (const auto & pm : ev->new_parameters) {
    rclcpp::Parameter p = rclcpp::Parameter::from_parameter_msg(pm);
    (void)rclcpp::to_string(p.get_parameter_value());
  }
  return 0;
}
