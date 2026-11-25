# Upstream's own gtest files (rclcpp/test/rclcpp) for the code the fuzz targets reach, built against
# the normal-flag rclcpp. Built here rather than through upstream's BUILD_TESTING tree because that
# tree needs mimick_vendor, test_msgs and performance_test_fixture, which the pinned image lacks.
cmake_minimum_required(VERSION 3.20)
project(rclcpp_unit_tests C CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(rcl_yaml_param_parser REQUIRED)
find_package(rcutils REQUIRED)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
add_subdirectory(/usr/src/googletest ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)
foreach(t test_parameter test_parameter_map test_parameter_events_filter)
  add_executable(${t} ${RCLCPP_TEST_DIR}/rclcpp/${t}.cpp)
  target_include_directories(${t} PRIVATE ${RCLCPP_TEST_DIR})
  target_link_libraries(${t} gtest_main rclcpp::rclcpp ${rcl_interfaces_TARGETS}
    rcl_yaml_param_parser::rcl_yaml_param_parser rcutils::rcutils)
endforeach()
