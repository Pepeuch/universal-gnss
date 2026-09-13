#pragma once

#include "universal_gnss_msgs/msg/rtcm_frame.hpp"

namespace universal_gnss_ros2::msg {

// Source-compatibility alias for existing C++ runtime consumers. The only ROS
// interface definition and generated type live in universal_gnss_msgs.
using RtcmFrame = universal_gnss_msgs::msg::RtcmFrame;

} // namespace universal_gnss_ros2::msg
