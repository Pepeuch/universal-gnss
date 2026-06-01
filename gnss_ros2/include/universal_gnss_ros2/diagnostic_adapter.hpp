#pragma once

#include <cstdint>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_health.hpp"

namespace universal_gnss_ros2
{

std::uint8_t ToDiagnosticLevel(universal_gnss::GnssDiagnosticSeverity severity);

diagnostic_msgs::msg::DiagnosticStatus ToDiagnosticStatusMessage(
    const universal_gnss::GnssDiagnosticEvent& event,
    const std::string& name_prefix = "universal_gnss",
    const std::string& hardware_id = "");

diagnostic_msgs::msg::DiagnosticStatus ToHealthDiagnosticStatusMessage(
    const universal_gnss::GnssHealthSummary& summary,
    const std::string& name = "universal_gnss/summary",
    const std::string& hardware_id = "");

diagnostic_msgs::msg::DiagnosticArray ToDiagnosticArrayMessage(
    const universal_gnss::GnssHealthSummary& summary,
    const std::string& name_prefix = "universal_gnss",
    const std::string& hardware_id = "");

}  // namespace universal_gnss_ros2
