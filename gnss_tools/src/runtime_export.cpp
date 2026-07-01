#include "universal_gnss_tools/runtime_export.hpp"

#include <iomanip>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_tools/gnss_stream_inspector.hpp"

namespace universal_gnss_tools
{

namespace
{

constexpr int kCoordinateOutputPrecision = 9;

const char* DescribeFixType(const universal_gnss::GnssFixType fix_type)
{
  switch (fix_type)
  {
    case universal_gnss::GnssFixType::kUnknown:
      return "unknown";
    case universal_gnss::GnssFixType::kNoFix:
      return "no_fix";
    case universal_gnss::GnssFixType::kFix:
      return "fix";
    case universal_gnss::GnssFixType::kRtkFloat:
      return "rtk_float";
    case universal_gnss::GnssFixType::kRtkFixed:
      return "rtk_fixed";
    case universal_gnss::GnssFixType::kDeadReckoning:
      return "dead_reckoning";
  }

  return "unknown";
}

const char* DescribeRtkMode(const std::optional<universal_gnss::GnssRtkMode>& rtk_mode)
{
  if (!rtk_mode.has_value())
  {
    return nullptr;
  }

  switch (*rtk_mode)
  {
    case universal_gnss::GnssRtkMode::kUnknown:
      return "unknown";
    case universal_gnss::GnssRtkMode::kNone:
      return "none";
    case universal_gnss::GnssRtkMode::kFloat:
      return "float";
    case universal_gnss::GnssRtkMode::kFixed:
      return "fixed";
  }

  return "unknown";
}

std::string EscapeJsonString(const std::string& text)
{
  std::ostringstream output;
  for (const unsigned char ch : text)
  {
    switch (ch)
    {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (ch < 0x20u)
        {
          output << "\\u00";
          constexpr char kHex[] = "0123456789ABCDEF";
          output << kHex[(ch >> 4u) & 0x0Fu] << kHex[ch & 0x0Fu];
        }
        else
        {
          output << static_cast<char>(ch);
        }
        break;
    }
  }
  return output.str();
}

std::string DescribeExportProtocol(const universal_gnss_protocols::ProtocolType protocol)
{
  switch (protocol)
  {
    case universal_gnss_protocols::ProtocolType::kNmea:
      return "NMEA";
    case universal_gnss_protocols::ProtocolType::kUbx:
      return "UBX";
    case universal_gnss_protocols::ProtocolType::kRtcm3:
      return "RTCM3";
    case universal_gnss_protocols::ProtocolType::kUnicore:
      return "UNICORE";
    case universal_gnss_protocols::ProtocolType::kUnknown:
    default:
      return "UNKNOWN";
  }
}

std::string DescribeExportMessage(const GnssReplayEvent& event)
{
  if (event.protocol == universal_gnss_protocols::ProtocolType::kNmea)
  {
    if (event.identity.size() >= 3u)
    {
      return event.identity.substr(event.identity.size() - 3u);
    }
    return event.identity;
  }

  if (event.protocol == universal_gnss_protocols::ProtocolType::kUbx)
  {
    return event.classification.empty() ? event.identity : event.classification;
  }

  if (event.protocol == universal_gnss_protocols::ProtocolType::kUnicore)
  {
    return event.identity;
  }

  return event.identity;
}

void WriteFieldPrefix(std::ostream& output, const bool pretty, bool& first_field)
{
  if (first_field)
  {
    output << '{';
    first_field = false;
    return;
  }

  output << (pretty ? ", " : ",");
}

template <typename T>
void WriteJsonOptionalNumber(std::ostream& output,
                             const char* key,
                             const std::optional<T>& value,
                             const bool pretty,
                             bool& first_field)
{
  WriteFieldPrefix(output, pretty, first_field);
  output << '"' << key << '"' << (pretty ? ": " : ":");
  if (value.has_value())
  {
    output << *value;
  }
  else
  {
    output << "null";
  }
}

void WriteJsonOptionalCoordinate(std::ostream& output,
                                 const char* key,
                                 const std::optional<double>& value,
                                 const bool pretty,
                                 bool& first_field)
{
  WriteFieldPrefix(output, pretty, first_field);
  output << '"' << key << '"' << (pretty ? ": " : ":");
  if (value.has_value())
  {
    std::ostringstream coordinate;
    coordinate << std::fixed << std::setprecision(kCoordinateOutputPrecision) << *value;
    output << coordinate.str();
  }
  else
  {
    output << "null";
  }
}

void WriteJsonString(std::ostream& output,
                     const char* key,
                     const std::string& value,
                     const bool pretty,
                     bool& first_field)
{
  WriteFieldPrefix(output, pretty, first_field);
  output << '"' << key << '"' << (pretty ? ": " : ":") << '"' << EscapeJsonString(value) << '"';
}

void WriteJsonBool(
    std::ostream& output, const char* key, const bool value, const bool pretty, bool& first_field)
{
  WriteFieldPrefix(output, pretty, first_field);
  output << '"' << key << '"' << (pretty ? ": " : ":") << (value ? "true" : "false");
}

void WriteJsonOptionalBool(std::ostream& output,
                           const char* key,
                           const std::optional<bool>& value,
                           const bool pretty,
                           bool& first_field)
{
  WriteFieldPrefix(output, pretty, first_field);
  output << '"' << key << '"' << (pretty ? ": " : ":");
  if (value.has_value())
  {
    output << (*value ? "true" : "false");
  }
  else
  {
    output << "null";
  }
}

void WriteRuntimeUpdateJson(std::ostream& output,
                            const GnssReplayEvent& event,
                            const RuntimeExportOptions& options)
{
  const universal_gnss::GnssRuntimeState& state = event.state_after_event;
  const bool pretty = options.pretty;
  bool first_field = true;

  WriteJsonOptionalNumber(
      output, "event_index", std::optional<std::size_t>(event.event_index), pretty, first_field);
  WriteJsonOptionalNumber(output, "timestamp_ns", state.timestamp_ns, pretty, first_field);
  WriteJsonString(output, "protocol", DescribeExportProtocol(event.protocol), pretty, first_field);
  WriteJsonString(output, "message", DescribeExportMessage(event), pretty, first_field);
  WriteJsonBool(output, "fix_valid", state.fix_valid, pretty, first_field);
  WriteJsonString(output, "fix_type", DescribeFixType(state.fix_type), pretty, first_field);

  const char* rtk_mode = DescribeRtkMode(state.rtk_mode);
  WriteFieldPrefix(output, pretty, first_field);
  output << "\"rtk_mode\"" << (pretty ? ": " : ":");
  if (rtk_mode == nullptr)
  {
    output << "null";
  }
  else
  {
    output << '"' << rtk_mode << '"';
  }

  WriteJsonOptionalCoordinate(output, "latitude_deg", state.latitude_deg, pretty, first_field);
  WriteJsonOptionalCoordinate(output, "longitude_deg", state.longitude_deg, pretty, first_field);
  WriteJsonOptionalNumber(output, "altitude_m", state.altitude_m, pretty, first_field);
  WriteJsonOptionalNumber(
      output, "horizontal_accuracy_m", state.horizontal_accuracy_m, pretty, first_field);
  WriteJsonOptionalNumber(
      output, "vertical_accuracy_m", state.vertical_accuracy_m, pretty, first_field);
  WriteJsonOptionalNumber(output, "hdop", state.hdop, pretty, first_field);
  WriteJsonOptionalNumber(output, "vdop", state.vdop, pretty, first_field);
  WriteJsonOptionalNumber(output, "satellites_used", state.satellites_used, pretty, first_field);
  WriteJsonOptionalNumber(
      output, "satellites_tracked", state.satellites_tracked, pretty, first_field);
  WriteJsonOptionalNumber(
      output, "satellites_visible", state.satellites_visible, pretty, first_field);
  WriteJsonOptionalNumber(output, "mean_cn0_dbhz", state.mean_cn0_db_hz, pretty, first_field);
  WriteJsonOptionalNumber(output, "max_cn0_dbhz", state.max_cn0_db_hz, pretty, first_field);
  WriteJsonOptionalNumber(output, "correction_age_s", state.correction_age_s, pretty, first_field);
  WriteJsonOptionalNumber(output, "heading_deg", state.heading_deg, pretty, first_field);
  WriteJsonOptionalBool(
      output, "dual_antenna_heading", state.dual_antenna_heading, pretty, first_field);
  WriteJsonOptionalBool(
      output, "interference_detected", state.interference_detected, pretty, first_field);
  WriteJsonOptionalBool(output, "jamming_detected", state.jamming_detected, pretty, first_field);

  output << '}';
}

}  // namespace

const char* DescribeRuntimeExportFormat(const RuntimeExportFormat format)
{
  switch (format)
  {
    case RuntimeExportFormat::kJsonl:
      return "jsonl";
  }

  return "jsonl";
}

std::string FormatRuntimeExportJsonl(const GnssReplayResult& replay_result,
                                     const RuntimeExportOptions& options)
{
  std::ostringstream output;
  WriteRuntimeExportJsonl(output, replay_result, options);
  return output.str();
}

std::size_t WriteRuntimeExportJsonl(std::ostream& output,
                                    const GnssReplayResult& replay_result,
                                    const RuntimeExportOptions& options)
{
  std::size_t lines_written = 0u;
  for (const auto& event : replay_result.events)
  {
    if (!event.produced_runtime_update)
    {
      continue;
    }

    WriteRuntimeUpdateJson(output, event, options);
    output << '\n';
    ++lines_written;
  }
  return lines_written;
}

}  // namespace universal_gnss_tools
