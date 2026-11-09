#include <type_traits>

#include <gtest/gtest.h>

#include "universal_gnss_msgs/msg/gnss_status.hpp"
#include "universal_gnss_msgs/msg/rtcm_frame.hpp"
#include "universal_gnss_msgs/srv/get_receiver_snapshot.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"
#include "universal_gnss_ros2/msg/rtcm_frame.hpp"
#include "universal_gnss_ros2/srv/get_receiver_snapshot.hpp"

static_assert(
    std::is_same_v<universal_gnss_ros2::msg::GnssStatus, universal_gnss_msgs::msg::GnssStatus>);
static_assert(
    std::is_same_v<universal_gnss_ros2::msg::RtcmFrame, universal_gnss_msgs::msg::RtcmFrame>);
static_assert(std::is_same_v<universal_gnss_ros2::srv::GetReceiverSnapshot,
                             universal_gnss_msgs::srv::GetReceiverSnapshot>);

TEST(InterfaceAliasesTest, CompatibilityHeadersAliasCanonicalGeneratedTypes)
{
  SUCCEED();
}
