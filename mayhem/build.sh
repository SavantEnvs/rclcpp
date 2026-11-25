#!/usr/bin/env bash
# mayhem/build.sh — build rclcpp twice and the fuzz harnesses + upstream unit tests.
#   1. rclcpp + rclcpp_components with $SANITIZER_FLAGS + SanCov (fuzzer-no-link) + DWARF 3 -> mayhem/build/fuzz-install
#   2. harnesses (libFuzzer + -standalone) linked against (1)        -> /mayhem/fuzz_*
#   3. rclcpp with normal flags + upstream gtest files                -> mayhem/build/test-*
# Everything resolves from the image (/opt/ros/rolling, /usr/src/googletest): no network.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
set +u; source /opt/ros/rolling/setup.bash; set -u
B="$SRC/mayhem/build"
mkdir -p "$B/compat/rcl"

# Compat shim: upstream rclcpp (since #3400) passes a const response to rcl_send_response, which the
# rcl shipped in the pinned ROS image still declares as `void *`. The two are ABI-identical (C, const
# does not change the symbol), so a generated copy of rcl/service.h with the const prototype is put
# first on the include path. Once the image's rcl has the const prototype, the shim is a no-op copy.
perl -0pe 's/(rcl_send_response\(\s*const rcl_service_t \* service,\s*rmw_request_id_t \* response_header,\s*)void \* ros_response\)/$1const void * ros_response)/' \
  /opt/ros/rolling/include/rcl/rcl/service.h > "$B/compat/rcl/service.h"
COMPAT="-I$B/compat"

# The image ENV sets LD_LIBRARY_PATH=/opt/ros/rolling/lib (as setup.bash does), which takes precedence
# over a RUNPATH. Link the harnesses and tests with an old-style RPATH so they load the librclcpp
# built here (sanitized / normal) and not the stock one in /opt/ros.
RPATH_FIRST="-Wl,--disable-new-dtags"

SAN_COV=""
[ -n "$SANITIZER_FLAGS" ] && SAN_COV="-fsanitize=fuzzer-no-link"

# 1) sanitized + coverage-instrumented rclcpp
cmake -S rclcpp -B "$B/fuzz" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX="$B/fuzz-install" \
  -DCMAKE_C_FLAGS="$SANITIZER_FLAGS $SAN_COV $DEBUG_FLAGS -O1 $COMPAT" \
  -DCMAKE_CXX_FLAGS="$SANITIZER_FLAGS $SAN_COV $DEBUG_FLAGS -O1 $COMPAT" \
  -DCMAKE_SHARED_LINKER_FLAGS="$SANITIZER_FLAGS"
cmake --build "$B/fuzz" -j"$MAYHEM_JOBS"
cmake --install "$B/fuzz" >/dev/null
# ... and rclcpp_components (ComponentManager), against the rclcpp above
cmake -S rclcpp_components -B "$B/fuzz-components" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$B/fuzz-install" -DCMAKE_INSTALL_PREFIX="$B/fuzz-install" \
  -DCMAKE_C_FLAGS="$SANITIZER_FLAGS $SAN_COV $DEBUG_FLAGS -O1 $COMPAT" \
  -DCMAKE_CXX_FLAGS="$SANITIZER_FLAGS $SAN_COV $DEBUG_FLAGS -O1 $COMPAT" \
  -DCMAKE_SHARED_LINKER_FLAGS="$SANITIZER_FLAGS $RPATH_FIRST" -DCMAKE_EXE_LINKER_FLAGS="$SANITIZER_FLAGS"
cmake --build "$B/fuzz-components" -j"$MAYHEM_JOBS"
cmake --install "$B/fuzz-components" >/dev/null

# 2) harnesses. LeakSanitizer is disabled at build time by linking mayhem/lsan_off.cc into every
#    fuzz and -standalone binary (ASan/UBSan stay on).
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$B/lsan_off.o"
cmake -S mayhem/harnesses -B "$B/harnesses" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$B/fuzz-install" \
  -DCMAKE_C_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O1" \
  -DCMAKE_CXX_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O1 $COMPAT" \
  -DFUZZ_ENGINE="$LIB_FUZZING_ENGINE" -DSTANDALONE_MAIN="$STANDALONE_FUZZ_MAIN" -DLSAN_OFF_OBJ="$B/lsan_off.o" \
  -DCMAKE_EXE_LINKER_FLAGS="$RPATH_FIRST" \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$SRC"
cmake --build "$B/harnesses" -j"$MAYHEM_JOBS"

# 3) normal-flag rclcpp + upstream unit tests (functional oracle for test.sh)
cmake -S rclcpp -B "$B/test-rclcpp" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$B/test-install" \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS $COMPAT" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS $COMPAT"
cmake --build "$B/test-rclcpp" -j"$MAYHEM_JOBS"
cmake --install "$B/test-rclcpp" >/dev/null
cmake -S mayhem/tests -B "$B/tests" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$B/test-install" -DRCLCPP_TEST_DIR="$SRC/rclcpp/test" \
  -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS $COMPAT" -DCMAKE_EXE_LINKER_FLAGS="$RPATH_FIRST"
cmake --build "$B/tests" -j"$MAYHEM_JOBS"
