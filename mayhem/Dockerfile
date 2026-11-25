# syntax=docker/dockerfile:1.7
#
# Commit image for rclcpp (ROS 2 C++ client library).
#
# STANDALONE form: rclcpp builds on top of a ROS 2 "rolling" install (rcl, rmw, rosidl, Fast DDS,
# rcl_interfaces, ...). ROS 2 rolling ships binaries for Ubuntu only, so the image starts from the
# official ros:rolling-ros-base image (pinned by digest) instead of the Debian org base, and
# replicates the org-base contract here: the mayhem/agent/sandbox identities, the build-contract ENV,
# clang with version-matched libFuzzer/ASan/UBSan runtimes, and LLVM's standalone fuzz driver
# (mayhem/StandaloneFuzzTargetMain.c, the same file the org base ships at /opt/mayhem).
FROM ros:rolling-ros-base@sha256:50c2d0ace849ef6f978c3395ad94f026c4c37e389c713ab714b99385982363c9

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

USER root
# clang + libclang-rt (libFuzzer/ASan/UBSan runtimes; the clang package alone cannot link
# -fsanitize=fuzzer). The ROS deps of rclcpp are already in ros-base.
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -yq --no-install-recommends clang-21 libclang-rt-21-dev lld-21 \
 && ln -sf /usr/bin/clang-21 /usr/local/bin/clang && ln -sf /usr/bin/clang++-21 /usr/local/bin/clang++ \
 && rm -rf /var/lib/apt/lists/*

# Identities (SPEC §4): Ubuntu ships an `ubuntu` user at uid 1000; replace it with `agent`.
RUN (userdel -r ubuntu 2>/dev/null || true) && (groupdel ubuntu 2>/dev/null || true) \
 && groupadd -g 1000 agent && useradd -m -u 1000 -g 1000 -s /bin/bash agent \
 && groupadd -g 2000 mayhem && useradd -m -u 2000 -g 2000 -s /bin/bash mayhem \
 && groupadd -g 10000 sandbox && useradd -M -u 10000 -g 10000 -s /usr/sbin/nologin sandbox \
 && mkdir -p /mayhem /opt/toolchains && chown 2000:2000 /mayhem

# Build contract (same as the org base) + the ROS environment that /opt/ros/rolling/setup.bash
# exports, so every process (build, test, fuzz target) sees it without sourcing.
ENV CC=clang CXX=clang++ \
    LIB_FUZZING_ENGINE=-fsanitize=fuzzer \
    SANITIZER_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g" \
    STANDALONE_FUZZ_MAIN=/mayhem/mayhem/StandaloneFuzzTargetMain.c \
    SRC=/mayhem \
    ROS_DISTRO=rolling ROS_VERSION=2 ROS_PYTHON_VERSION=3 \
    AMENT_PREFIX_PATH=/opt/ros/rolling \
    PYTHONPATH=/opt/ros/rolling/lib/python3.14/site-packages \
    LD_LIBRARY_PATH=/opt/ros/rolling/lib/x86_64-linux-gnu:/opt/ros/rolling/lib \
    PATH=/opt/ros/rolling/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    ROS_AUTOMATIC_DISCOVERY_RANGE=OFF

ENTRYPOINT []
USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN bash mayhem/build.sh

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
CMD ["bash"]
