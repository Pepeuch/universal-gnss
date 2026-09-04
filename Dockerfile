# syntax=docker/dockerfile:1

ARG ROS_DISTRO=kilted
FROM ros:${ROS_DISTRO}-ros-base AS builder

ARG ROS_DISTRO

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libssl-dev \
    python3-colcon-common-extensions \
    ros-${ROS_DISTRO}-builtin-interfaces \
    ros-${ROS_DISTRO}-diagnostic-msgs \
    ros-${ROS_DISTRO}-launch \
    ros-${ROS_DISTRO}-launch-ros \
    ros-${ROS_DISTRO}-rclcpp \
    ros-${ROS_DISTRO}-rosidl-default-generators \
    ros-${ROS_DISTRO}-rosidl-default-runtime \
    ros-${ROS_DISTRO}-sensor-msgs \
    ros-${ROS_DISTRO}-std-srvs \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace/src/universal_gnss

RUN source /opt/ros/${ROS_DISTRO}/setup.bash \
 && colcon build --merge-install \
      --base-paths src/universal_gnss/gnss_ros2 \
      --packages-select universal_gnss_ros2 \
      --cmake-args -DCMAKE_BUILD_TYPE=Release \
 && rm -rf build log src

FROM ros:${ROS_DISTRO}-ros-base AS runtime

ARG ROS_DISTRO
ARG VERSION=dev
ARG REVISION=unknown
ARG APP_UID=1000
ARG APP_GID=1000

LABEL org.opencontainers.image.title="Universal GNSS ROS 2" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.revision="${REVISION}"

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    ca-certificates \
    procps \
    tini \
    ros-${ROS_DISTRO}-builtin-interfaces \
    ros-${ROS_DISTRO}-diagnostic-msgs \
    ros-${ROS_DISTRO}-launch \
    ros-${ROS_DISTRO}-launch-ros \
    ros-${ROS_DISTRO}-rclcpp \
    ros-${ROS_DISTRO}-rosidl-default-runtime \
    ros-${ROS_DISTRO}-sensor-msgs \
    ros-${ROS_DISTRO}-std-srvs \
 && if apt-cache show libssl3 > /dev/null 2>&1; then apt-get install -y --no-install-recommends libssl3; else apt-get install -y --no-install-recommends libssl3t64; fi \
 && rm -rf /var/lib/apt/lists/* \
 && install --directory --owner=${APP_UID} --group=${APP_GID} /var/log/universal_gnss

COPY --from=builder /workspace/install /opt/universal_gnss/install
COPY docker/entrypoint.sh /usr/local/bin/universal-gnss-entrypoint

RUN chmod 0755 /usr/local/bin/universal-gnss-entrypoint

ENV ROS_DISTRO=${ROS_DISTRO} \
    HOME=/tmp \
    ROS_LOG_DIR=/var/log/universal_gnss \
    UNIVERSAL_GNSS_PARAMETERS_FILE=/etc/universal_gnss/parameters.yaml

WORKDIR /opt/universal_gnss
USER ${APP_UID}:${APP_GID}

STOPSIGNAL SIGINT

# This verifies only that both launch-managed ROS processes are present. It does
# not assert receiver transport, observation freshness, NTRIP, RTCM, or RTK health.
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD pgrep -f '[r]eceiver_node' > /dev/null && pgrep -f '[n]trip_node' > /dev/null || exit 1

ENTRYPOINT ["/usr/bin/tini", "-g", "--", "/usr/local/bin/universal-gnss-entrypoint"]
