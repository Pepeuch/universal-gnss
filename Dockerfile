# syntax=docker/dockerfile:1

ARG ROS_DISTRO=kilted
FROM ros:${ROS_DISTRO}-ros-base AS builder

ARG ROS_DISTRO
ARG APP_UID=1000
ARG APP_GID=1000

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
RUN chown "${APP_UID}:${APP_GID}" /workspace
COPY --chown=${APP_UID}:${APP_GID} . /workspace/src/universal_gnss

USER ${APP_UID}:${APP_GID}
ENV HOME=/tmp

RUN source /opt/ros/${ROS_DISTRO}/setup.bash \
 && colcon build --merge-install \
      --base-paths src/universal_gnss/gnss_ros2 \
      --packages-select universal_gnss_ros2 \
      --cmake-args -DCMAKE_BUILD_TYPE=Release

USER root
RUN rm -rf /workspace/build /workspace/log /workspace/src

FROM ros:${ROS_DISTRO}-ros-base AS runtime

ARG ROS_DISTRO
ARG VERSION=dev
# Supply a source revision for release/CI builds. It is deliberately empty for
# ad-hoc builds rather than claiming an unavailable revision.
ARG REVISION
ARG DESCRIPTION="Universal GNSS ROS 2 production container"
ARG SOURCE=https://github.com/Pepeuch/universal-gnss
# Supply the source commit's RFC 3339 timestamp for release/CI builds. It is
# deliberately empty for ad-hoc builds rather than inventing a build time.
ARG CREATED
ARG APP_UID=1000
ARG APP_GID=1000

LABEL org.opencontainers.image.title="Universal GNSS ROS 2" \
      org.opencontainers.image.description="${DESCRIPTION}" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.revision="${REVISION}" \
      org.opencontainers.image.source="${SOURCE}" \
      org.opencontainers.image.created="${CREATED}"

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
    ros-${ROS_DISTRO}-rclpy \
    ros-${ROS_DISTRO}-rclcpp \
    ros-${ROS_DISTRO}-rosidl-default-runtime \
    ros-${ROS_DISTRO}-sensor-msgs \
    ros-${ROS_DISTRO}-std-srvs \
 && if apt-cache show libssl3 > /dev/null 2>&1; then apt-get install -y --no-install-recommends libssl3; else apt-get install -y --no-install-recommends libssl3t64; fi \
 && rm -rf /var/lib/apt/lists/* \
 && install --directory --owner=${APP_UID} --group=${APP_GID} \
    /var/log/universal_gnss \
    /var/lib/universal_gnss/export

COPY --from=builder /workspace/install /opt/universal_gnss/install
COPY docker/entrypoint.sh /usr/local/bin/universal-gnss-entrypoint
COPY docker/healthcheck.sh /usr/local/bin/universal-gnss-healthcheck
COPY docker/healthcheck.py /usr/local/libexec/universal-gnss-healthcheck.py
COPY scripts/collect_support_snapshot.py /usr/local/bin/universal-gnss-support-snapshot

RUN chmod 0755 \
    /usr/local/bin/universal-gnss-entrypoint \
    /usr/local/bin/universal-gnss-healthcheck \
    /usr/local/libexec/universal-gnss-healthcheck.py \
    /usr/local/bin/universal-gnss-support-snapshot

ENV ROS_DISTRO=${ROS_DISTRO} \
    HOME=/tmp \
    ROS_LOG_DIR=/var/log/universal_gnss \
    UNIVERSAL_GNSS_EXPORT_DIR=/var/lib/universal_gnss/export \
    RCUTILS_COLORIZED_OUTPUT=0 \
    RCUTILS_CONSOLE_OUTPUT_FORMAT="timestamp={time} severity={severity} logger={name} message={message}" \
    UNIVERSAL_GNSS_VERSION=${VERSION} \
    UNIVERSAL_GNSS_REVISION=${REVISION} \
    UNIVERSAL_GNSS_PARAMETERS_FILE=/etc/universal_gnss/parameters.yaml

WORKDIR /opt/universal_gnss
USER ${APP_UID}:${APP_GID}

STOPSIGNAL SIGINT

# This verifies bounded ROS service responsiveness for both enabled components.
# It does not assert receiver transport, observation freshness, NTRIP, RTCM, or RTK health.
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD ["/usr/local/bin/universal-gnss-healthcheck", "--timeout", "2"]

ENTRYPOINT ["/usr/bin/tini", "-g", "--", "/usr/local/bin/universal-gnss-entrypoint"]
