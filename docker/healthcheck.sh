#!/usr/bin/env bash
set -eo pipefail

source "/opt/ros/${ROS_DISTRO:?ROS_DISTRO must be set}/setup.bash"
source /opt/universal_gnss/install/setup.bash
set -u
exec python3 /usr/local/libexec/universal-gnss-healthcheck.py "$@"
