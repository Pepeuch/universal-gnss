#include "universal_gnss_ros2/diagnostic_adapter.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "diagnostic_msgs/msg/key_value.hpp"
#include "universal_gnss_ros2/gnss_status_adapter.hpp"

namespace universal_gnss_ros2
{

namespace
{

using diagnostic_msgs::msg::DiagnosticArray;
using diagnostic_msgs::msg::DiagnosticStatus;
using diagnostic_msgs::msg::KeyValue;

const char* DescribeDiagnosticSeverity(const universal_gnss::GnssDiagnosticSeverity severity)
{
  using universal_gnss::GnssDiagnosticSeverity;

  switch (severity)
  {
    case GnssDiagnosticSeverity::kOk:
      return "ok";
    case GnssDiagnosticSeverity::kInfo:
      return "info";
    case GnssDiagnosticSeverity::kWarning:
      return "warning";
    case GnssDiagnosticSeverity::kError:
      return "error";
    case GnssDiagnosticSeverity::kStale:
      return "stale";
    case GnssDiagnosticSeverity::kUnknown:
    default:
      return "unknown";
  }
}

const char* DescribeDiagnosticCategory(const universal_gnss::GnssDiagnosticCategory category)
{
  using universal_gnss::GnssDiagnosticCategory;

  switch (category)
  {
    case GnssDiagnosticCategory::kRuntime:
      return "runtime";
    case GnssDiagnosticCategory::kParser:
      return "parser";
    case GnssDiagnosticCategory::kTransport:
      return "transport";
    case GnssDiagnosticCategory::kCorrection:
      return "correction";
    case GnssDiagnosticCategory::kReceiver:
      return "receiver";
    case GnssDiagnosticCategory::kConfiguration:
      return "configuration";
    case GnssDiagnosticCategory::kTiming:
      return "timing";
    default:
      return "runtime";
  }
}

std::string BoolString(const bool value)
{
  return value ? "true" : "false";
}

KeyValue MakeKeyValue(std::string key, std::string value)
{
  KeyValue entry;
  entry.key = std::move(key);
  entry.value = std::move(value);
  return entry;
}

std::optional<universal_gnss::GnssTimestampNs> LatestDiagnosticTimestamp(
    const universal_gnss::GnssDiagnosticEvents& events)
{
  std::optional<universal_gnss::GnssTimestampNs> latest{};
  for (const auto& event : events)
  {
    if (!event.timestamp_ns.has_value())
    {
      continue;
    }

    if (!latest.has_value() || *event.timestamp_ns > *latest)
    {
      latest = event.timestamp_ns;
    }
  }
  return latest;
}

}  // namespace

std::uint8_t ToDiagnosticLevel(const universal_gnss::GnssDiagnosticSeverity severity)
{
  switch (severity)
  {
    case universal_gnss::GnssDiagnosticSeverity::kOk:
    case universal_gnss::GnssDiagnosticSeverity::kInfo:
      return DiagnosticStatus::OK;
    case universal_gnss::GnssDiagnosticSeverity::kWarning:
    case universal_gnss::GnssDiagnosticSeverity::kUnknown:
      return DiagnosticStatus::WARN;
    case universal_gnss::GnssDiagnosticSeverity::kError:
      return DiagnosticStatus::ERROR;
    case universal_gnss::GnssDiagnosticSeverity::kStale:
      return DiagnosticStatus::STALE;
  }

  return DiagnosticStatus::WARN;
}

DiagnosticStatus ToDiagnosticStatusMessage(const universal_gnss::GnssDiagnosticEvent& event,
                                           const std::string& name_prefix,
                                           const std::string& hardware_id)
{
  DiagnosticStatus status;
  status.level = ToDiagnosticLevel(event.severity);
  status.name = event.code.empty() ? (name_prefix + "/diagnostic") : (name_prefix + "/" + event.code);
  status.message = event.message.empty() ? "universal_gnss diagnostic event" : event.message;
  status.hardware_id = hardware_id;

  status.values.push_back(
      MakeKeyValue("original_severity", DescribeDiagnosticSeverity(event.severity)));
  status.values.push_back(
      MakeKeyValue("category", DescribeDiagnosticCategory(event.category)));
  status.values.push_back(MakeKeyValue("code", event.code));
  if (event.source.has_value())
  {
    status.values.push_back(MakeKeyValue("source", *event.source));
  }
  if (event.timestamp_ns.has_value())
  {
    status.values.push_back(
        MakeKeyValue("timestamp_ns", std::to_string(*event.timestamp_ns)));
  }
  return status;
}

DiagnosticStatus ToHealthDiagnosticStatusMessage(const universal_gnss::GnssHealthSummary& summary,
                                                 const std::string& name,
                                                 const std::string& hardware_id)
{
  DiagnosticStatus status;
  status.level = ToDiagnosticLevel(summary.overall_severity);
  status.name = name;
  status.message = "universal_gnss health summary";
  status.hardware_id = hardware_id;

  status.values.push_back(
      MakeKeyValue("overall_severity", DescribeDiagnosticSeverity(summary.overall_severity)));
  status.values.push_back(MakeKeyValue("fix_valid", BoolString(summary.fix_valid)));
  status.values.push_back(MakeKeyValue("rtk_available", BoolString(summary.rtk_available)));
  status.values.push_back(
      MakeKeyValue("correction_available", BoolString(summary.correction_available)));
  status.values.push_back(
      MakeKeyValue("receiver_healthy", BoolString(summary.receiver_healthy)));
  status.values.push_back(
      MakeKeyValue("transport_healthy", BoolString(summary.transport_healthy)));
  status.values.push_back(MakeKeyValue("parser_healthy", BoolString(summary.parser_healthy)));
  status.values.push_back(MakeKeyValue("stale_data", BoolString(summary.stale_data)));
  status.values.push_back(
      MakeKeyValue("event_count", std::to_string(summary.events.size())));
  return status;
}

DiagnosticArray ToDiagnosticArrayMessage(const universal_gnss::GnssHealthSummary& summary,
                                         const std::string& name_prefix,
                                         const std::string& hardware_id)
{
  DiagnosticArray array;
  array.header.stamp = ToRosTime(LatestDiagnosticTimestamp(summary.events));
  array.status.push_back(
      ToHealthDiagnosticStatusMessage(summary, name_prefix + "/summary", hardware_id));
  for (const auto& event : summary.events)
  {
    array.status.push_back(ToDiagnosticStatusMessage(event, name_prefix, hardware_id));
  }
  return array;
}

}  // namespace universal_gnss_ros2
