#pragma once

#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"

namespace universal_gnss_ros2
{

sensor_msgs::msg::NavSatFix ToNavSatFixMessage(const universal_gnss::GnssRuntimeState& state);

}  // namespace universal_gnss_ros2
