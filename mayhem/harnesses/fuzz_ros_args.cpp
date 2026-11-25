// ROS command-line arguments (--ros-args: -p name:=value, -r remap rules, --params-file,
// --log-level, --enclave, -e ...). The fuzz input is split on NUL into argv after
// "prog --ros-args" and parsed by rclcpp::remove_ros_arguments and by
// NodeOptions::arguments().get_rcl_node_options() (rcl argument parser + rclcpp's wrapper).
// Invalid arguments raise rclcpp exceptions (std::exception) by contract; those are tolerated.
#include <rclcpp/node_options.hpp>
#include <rclcpp/utilities.hpp>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * d, size_t n) {
  std::vector<std::string> args{"prog", "--ros-args"};
  std::string cur;
  for (size_t i = 0; i < n; i++) {
    if (d[i] == 0) { args.push_back(cur); cur.clear(); } else cur.push_back((char)d[i]);
  }
  args.push_back(cur);
  std::vector<const char *> argv;
  for (auto & a : args) argv.push_back(a.c_str());
  try {
    auto rest = rclcpp::remove_ros_arguments((int)argv.size(), argv.data());
    (void)rest;
  } catch (const std::exception &) {
  }
  try {
    rclcpp::NodeOptions opts;
    opts.use_global_arguments(false);
    opts.arguments(std::vector<std::string>(args.begin() + 1, args.end()));
    (void)opts.get_rcl_node_options();
  } catch (const std::exception &) {
  }
  return 0;
}
