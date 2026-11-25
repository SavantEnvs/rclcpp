// Topic/service names and type strings (node remaps, user configuration, introspection data):
// the fuzz input is split on NUL into name / node name / namespace / type string and given to
// rclcpp::expand_topic_or_service_name (rcl expansion + rcl/rmw validation + exception mapping)
// and to rclcpp's type-string helpers (extract_type_identifier, get_typesupport_library_path,
// which resolves through the ament index read-only). Invalid names raise std::exception by contract.
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * d, size_t n) {
  std::vector<std::string> f(1);
  for (size_t i = 0; i < n; i++) {
    if (d[i] == 0 && f.size() < 4) f.emplace_back(); else f.back().push_back((char)d[i]);
  }
  f.resize(4);
  for (bool is_service : {false, true}) {
    try {
      (void)rclcpp::expand_topic_or_service_name(f[0], f[1].empty() ? "node" : f[1],
                                                 f[2].empty() ? "/" : f[2], is_service);
    } catch (const std::exception &) {
    }
  }
  try {
    auto t = rclcpp::extract_type_identifier(f[3]);
    (void)rclcpp::get_typesupport_library_path(std::get<0>(t), "rosidl_typesupport_cpp");
  } catch (const std::exception &) {
  }
  return 0;
}
