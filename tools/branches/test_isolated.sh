#!/usr/bin/env bash
# Build/test only a generated tree, streamed into a cached image without source mounts.
set -euo pipefail
if [[ $# != 3 || ( "$1" != kilted && "$1" != lyrical ) ]]; then
  echo "usage: $0 kilted|lyrical <generated-source> <cached-build-image>" >&2
  exit 2
fi
ros_distro="$1"
source_tree="$2"
image="$3"
if [[ "$(id -u)" == 0 ]]; then
  echo "Run as the normal project user." >&2
  exit 1
fi
test -f "${source_tree}/.ug-distribution.json"
docker image inspect "${image}" >/dev/null
# No --volume, bind mounts, host network, Docker socket, device or credentials.
# Empty environment prevents accidental reuse of an installed UG overlay.
tar -C "${source_tree}" -cf - . | docker run --rm -i --pull=never \
  --network none --user "$(id -u):$(id -g)" --workdir /tmp --tmpfs /workspaces:rw,nosuid,nodev \
  --entrypoint /usr/bin/env "${image}" -i \
  PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  HOME=/tmp/ug-home COLCON_HOME=/tmp/ug-colcon ROS_LOG_DIR=/tmp/ug-ros-log \
  ROS_LOCALHOST_ONLY=1 CMAKE_BUILD_PARALLEL_LEVEL=3 \
  bash --noprofile --norc -c '
    set -eo pipefail
    test "$(id -u)" != 0
    mkdir -p "$HOME" /tmp/ug-work/source
    tar -xf - -C /tmp/ug-work/source
    cd /tmp/ug-work
    test ! -e source/gnss_runtime
    test ! -e source/core/gnss_runtime
    test ! -e source/gnss_mavros
    test ! -e source/.git
    test ! -e source/.github
    test ! -e /workspaces/universal-gnss
    source "/opt/ros/$1/setup.bash"
    test "$ROS_DISTRO" = "$1"
    package_paths=(source/universal_gnss_msgs source/gnss_ros2)
    packages=(universal_gnss_msgs universal_gnss_ros2)
    if test -d source/adapters/mavros/gnss_mavros; then
      if test -f /opt/mavros/local_setup.bash; then
        test ! -e /opt/mavros/share/universal_gnss_ros2
        source /opt/mavros/local_setup.bash
      fi
      package_paths+=(source/adapters/mavros/gnss_mavros)
      packages+=(universal_gnss_mavros)
    fi
    colcon list --base-paths "${package_paths[@]}"
    colcon build --base-paths "${package_paths[@]}" \
      --packages-select "${packages[@]}" \
      --executor sequential --cmake-args -DCMAKE_BUILD_TYPE=Release
    source install/setup.bash
    test -z "$(ros2 pkg executables universal_gnss_msgs)"
    ros2 interface show universal_gnss_msgs/msg/GnssStatus >/dev/null
    ros2 interface show universal_gnss_msgs/msg/RtcmFrame >/dev/null
    ros2 interface show universal_gnss_msgs/srv/GetReceiverSnapshot >/dev/null
    ros2 pkg executables universal_gnss_ros2
    colcon test --packages-select "${packages[@]}" \
      --executor sequential --return-code-on-test-failure
    colcon test-result --verbose
  ' ug-isolated "$ros_distro"
