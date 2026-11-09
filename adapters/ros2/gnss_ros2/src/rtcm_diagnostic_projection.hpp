#pragma once

#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"

namespace universal_gnss_ros2
{

diagnostic_msgs::msg::DiagnosticStatus ToRtcmSemanticDiagnosticStatusMessage(
    const universal_gnss_protocols::RtcmSemanticObservation& observation,
    const std::string& name_prefix = "universal_gnss",
    const std::string& hardware_id = "");

void AppendRtcmSemanticObservationStatuses(
    diagnostic_msgs::msg::DiagnosticArray& array,
    const universal_gnss_protocols::RtcmSemanticObservations& observations,
    const std::string& name_prefix = "universal_gnss",
    const std::string& hardware_id = "");

}  // namespace universal_gnss_ros2
