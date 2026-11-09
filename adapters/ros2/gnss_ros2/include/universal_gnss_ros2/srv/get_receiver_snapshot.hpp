#pragma once

#include "universal_gnss_msgs/srv/get_receiver_snapshot.hpp"

namespace universal_gnss_ros2::srv
{

// Source-compatibility alias for existing C++ runtime consumers. The only ROS
// service definition and generated type live in universal_gnss_msgs.
using GetReceiverSnapshot = universal_gnss_msgs::srv::GetReceiverSnapshot;

}  // namespace universal_gnss_ros2::srv
