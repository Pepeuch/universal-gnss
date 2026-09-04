#!/usr/bin/env bash
set -eo pipefail

: "${ROS_DISTRO:?ROS_DISTRO must be set}"
: "${ROS_LOG_DIR:=/var/log/universal_gnss}"
: "${UNIVERSAL_GNSS_PARAMETERS_FILE:=/etc/universal_gnss/parameters.yaml}"

mkdir -p "${ROS_LOG_DIR}"
if [[ ! -w "${ROS_LOG_DIR}" ]]; then
  echo "ROS_LOG_DIR is not writable: ${ROS_LOG_DIR}" >&2
  exit 1
fi

source "/opt/ros/${ROS_DISTRO}/setup.bash"
source /opt/universal_gnss/install/setup.bash

set -u

if [[ "$#" -eq 0 ]]; then
  if [[ ! -r "${UNIVERSAL_GNSS_PARAMETERS_FILE}" ]]; then
    echo "Universal GNSS parameter file is not readable: ${UNIVERSAL_GNSS_PARAMETERS_FILE}" >&2
    exit 1
  fi

  exec ros2 launch universal_gnss_ros2 receiver_and_ntrip.launch.py \
    "parameters_file:=${UNIVERSAL_GNSS_PARAMETERS_FILE}"
fi

exec "$@"
