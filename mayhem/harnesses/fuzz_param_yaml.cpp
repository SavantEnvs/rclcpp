// Parameter YAML files (--params-file / NodeOptions parameter overrides): the fuzz input is written
// to a scratch .yaml and loaded through rclcpp::parameter_map_from_yaml_file, then resolved for a
// node with parameters_from_map (wildcard node-name matching) and printed. Malformed files are
// reported by the API with exceptions derived from std::exception; those are the documented error
// path and are tolerated.
#include <rclcpp/parameter_map.hpp>
#include <rclcpp/parameter.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static std::string scratch;

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  const char * t = getenv("TMPDIR");
  std::string tmpl = std::string(t && *t ? t : "/tmp") + "/rclcpp_fuzz_XXXXXX.yaml";
  std::vector<char> buf(tmpl.begin(), tmpl.end()); buf.push_back(0);
  int fd = mkstemps(buf.data(), 5);
  if (fd < 0) abort();
  close(fd);
  scratch = buf.data();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * d, size_t n) {
  FILE * f = fopen(scratch.c_str(), "wb");
  if (!f) abort();
  if (n) fwrite(d, 1, n, f);
  fclose(f);
  try {
    rclcpp::ParameterMap map = rclcpp::parameter_map_from_yaml_file(scratch);
    for (const char * node : {"/ns/node", "/node", "/a/b/c"}) {
      for (const auto & p : rclcpp::parameters_from_map(map, node)) {
        (void)std::to_string(p);
        (void)p.to_parameter_msg();
      }
    }
    for (const auto & kv : rclcpp::parameters_from_map(map, nullptr)) {
      (void)kv;
    }
  } catch (const std::exception &) {
  }
  return 0;
}
