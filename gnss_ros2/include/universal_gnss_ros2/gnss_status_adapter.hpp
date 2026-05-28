#pragma once

#include "builtin_interfaces/msg/time.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"

namespace universal_gnss_ros2
{

builtin_interfaces::msg::Time ToRosTime(
    const std::optional<universal_gnss::GnssTimestampNs>& timestamp_ns);

bool HasValidCapabilityValueInvariant(const universal_gnss_ros2::msg::GnssStatus& message);

universal_gnss_ros2::msg::GnssStatus ToGnssStatusMessage(
    const universal_gnss::GnssRuntimeState& state);

}  // namespace universal_gnss_ros2
