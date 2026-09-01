#include "universal_gnss_tools/runtime_state_format.hpp"

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace universal_gnss_tools
{

namespace
{

constexpr int kCoordinateOutputPrecision = 9;

const char* ToString(const universal_gnss::GnssFixType fix_type)
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

const char* ToString(const universal_gnss::GnssRtkMode rtk_mode)
{
  switch (rtk_mode)
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

const char* ToString(const universal_gnss::GnssBaselineSolutionStatus status)
{
  switch (status)
  {
    case universal_gnss::GnssBaselineSolutionStatus::kUnknown:
      return "unknown";
    case universal_gnss::GnssBaselineSolutionStatus::kComputed:
      return "computed";
    case universal_gnss::GnssBaselineSolutionStatus::kNotSolved:
      return "not_solved";
    case universal_gnss::GnssBaselineSolutionStatus::kInsufficientObservations:
      return "insufficient_observations";
    case universal_gnss::GnssBaselineSolutionStatus::kNoConvergence:
      return "no_convergence";
    case universal_gnss::GnssBaselineSolutionStatus::kOutOfTolerance:
      return "out_of_tolerance";
    case universal_gnss::GnssBaselineSolutionStatus::kCovarianceTraceExceeded:
      return "covariance_trace_exceeded";
    case universal_gnss::GnssBaselineSolutionStatus::kNotConfigured:
      return "not_configured";
  }

  return "unknown";
}

std::string EscapeJson(const std::string& value)
{
  std::ostringstream escaped;
  for (const char character : value)
  {
    switch (character)
    {
      case '\\':
        escaped << "\\\\";
        break;
      case '"':
        escaped << "\\\"";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        escaped << character;
        break;
    }
  }
  return escaped.str();
}

template <typename T>
void AppendOptionalInteger(std::ostringstream& stream,
                           const char* label,
                           const std::optional<T>& value)
{
  if (value.has_value())
  {
    stream << ' ' << label << '=' << *value;
  }
}

template <typename T>
void AppendOptionalFloat(std::ostringstream& stream,
                         const char* label,
                         const std::optional<T>& value,
                         const int precision)
{
  if (value.has_value())
  {
    stream << ' ' << label << '=' << std::fixed << std::setprecision(precision) << *value;
  }
}

void AppendOptionalBool(std::ostringstream& stream,
                        const char* label,
                        const std::optional<bool>& value)
{
  if (value.has_value())
  {
    stream << ' ' << label << '=' << (*value ? "true" : "false");
  }
}

template <typename T>
void AppendOptionalEnum(std::ostringstream& stream,
                        const char* label,
                        const std::optional<T>& value,
                        const char* (*describe)(T))
{
  if (value.has_value())
  {
    stream << ' ' << label << '=' << describe(*value);
  }
}

template <typename T>
void AppendJsonOptionalNumber(std::ostringstream& stream,
                              const char* label,
                              const std::optional<T>& value)
{
  stream << '"' << label << "\":";
  if (value.has_value())
  {
    stream << *value;
  }
  else
  {
    stream << "null";
  }
}

void AppendJsonOptionalCoordinate(std::ostringstream& stream,
                                  const char* label,
                                  const std::optional<double>& value)
{
  stream << '"' << label << "\":";
  if (value.has_value())
  {
    std::ostringstream coordinate;
    coordinate << std::fixed << std::setprecision(kCoordinateOutputPrecision) << *value;
    stream << coordinate.str();
  }
  else
  {
    stream << "null";
  }
}

void AppendJsonOptionalBool(std::ostringstream& stream,
                            const char* label,
                            const std::optional<bool>& value)
{
  stream << '"' << label << "\":";
  if (value.has_value())
  {
    stream << (*value ? "true" : "false");
  }
  else
  {
    stream << "null";
  }
}

template <typename T>
void AppendJsonOptionalEnum(std::ostringstream& stream,
                            const char* label,
                            const std::optional<T>& value,
                            const char* (*describe)(T))
{
  stream << '"' << label << "\":";
  if (value.has_value())
  {
    stream << '"' << EscapeJson(describe(*value)) << '"';
  }
  else
  {
    stream << "null";
  }
}

}  // namespace

std::string FormatRuntimeStateCompact(
    const universal_gnss::GnssRuntimeState& state,
    const std::optional<universal_gnss_driver::ReceiverSessionKind> selected_session_kind)
{
  std::ostringstream stream;
  stream << std::boolalpha;
  stream << "session=";
  if (selected_session_kind.has_value())
  {
    stream << universal_gnss_driver::ToString(*selected_session_kind);
  }
  else
  {
    stream << "undecided";
  }

  if (state.timestamp_ns.has_value())
  {
    stream << " ts_ns=" << *state.timestamp_ns;
  }
  stream << " fix_valid=" << state.fix_valid;
  stream << " fix_type=" << ToString(state.fix_type);
  stream << " rtk_mode=" << (state.rtk_mode.has_value() ? ToString(*state.rtk_mode) : "unknown");

  AppendOptionalFloat(stream, "lat_deg", state.latitude_deg, kCoordinateOutputPrecision);
  AppendOptionalFloat(stream, "lon_deg", state.longitude_deg, kCoordinateOutputPrecision);
  AppendOptionalFloat(stream, "alt_m", state.altitude_m, 3);
  AppendOptionalFloat(stream, "h_acc_m", state.horizontal_accuracy_m, 3);
  AppendOptionalFloat(stream, "v_acc_m", state.vertical_accuracy_m, 3);
  AppendOptionalFloat(stream, "hdop", state.hdop, 2);
  AppendOptionalFloat(stream, "vdop", state.vdop, 2);
  AppendOptionalInteger(stream, "sats_used", state.satellites_used);
  AppendOptionalInteger(stream, "sats_tracked", state.satellites_tracked);
  AppendOptionalInteger(stream, "sats_visible", state.satellites_visible);
  AppendOptionalFloat(stream, "cn0_mean_db_hz", state.mean_cn0_db_hz, 1);
  AppendOptionalFloat(stream, "cn0_max_db_hz", state.max_cn0_db_hz, 1);
  AppendOptionalFloat(stream, "corr_age_s", state.correction_age_s, 2);
  if (state.utc_date.has_value())
  {
    stream << " utc_date=" << state.utc_date->year << '-' << static_cast<unsigned int>(state.utc_date->month)
           << '-' << static_cast<unsigned int>(state.utc_date->day);
  }
  if (state.utc_time.has_value())
  {
    stream << " utc_time=" << static_cast<unsigned int>(state.utc_time->hour) << ':'
           << static_cast<unsigned int>(state.utc_time->minute) << ':'
           << static_cast<unsigned int>(state.utc_time->second) << '.' << state.utc_time->nanosecond;
  }
  AppendOptionalFloat(stream, "speed_over_ground_m_s", state.speed_over_ground_m_s, 3);
  AppendOptionalFloat(stream, "course_over_ground_deg", state.course_over_ground_deg, 2);
  AppendOptionalFloat(stream, "heading_deg", state.heading_deg, 2);
  AppendOptionalBool(stream, "dual_antenna_heading", state.dual_antenna_heading);
  AppendOptionalBool(stream, "dual_antenna_baseline", state.dual_antenna_baseline);
  AppendOptionalFloat(stream, "baseline_azimuth_deg", state.baseline_azimuth_deg, 2);
  AppendOptionalFloat(stream, "baseline_pitch_deg", state.baseline_pitch_deg, 2);
  AppendOptionalFloat(stream, "baseline_length_m", state.baseline_length_m, 3);
  AppendOptionalEnum(stream,
                     "baseline_solution_status",
                     state.baseline_solution_status,
                     ToString);
  AppendOptionalBool(stream, "interference", state.interference_detected);
  AppendOptionalBool(stream, "jamming", state.jamming_detected);

  return stream.str();
}

std::string FormatRuntimeStateJson(
    const universal_gnss::GnssRuntimeState& state,
    const std::optional<universal_gnss_driver::ReceiverSessionKind> selected_session_kind)
{
  std::ostringstream stream;
  stream << std::boolalpha;
  stream << '{';
  stream << "\"selected_session\":";
  if (selected_session_kind.has_value())
  {
    stream << '"' << EscapeJson(universal_gnss_driver::ToString(*selected_session_kind)) << '"';
  }
  else
  {
    stream << "null";
  }
  stream << ',';

  AppendJsonOptionalNumber(stream, "timestamp_ns", state.timestamp_ns);
  stream << ',';
  stream << "\"fix_valid\":" << (state.fix_valid ? "true" : "false") << ',';
  stream << "\"fix_type\":\"" << ToString(state.fix_type) << "\",";
  stream << "\"rtk_mode\":";
  if (state.rtk_mode.has_value())
  {
    stream << '"' << ToString(*state.rtk_mode) << '"';
  }
  else
  {
    stream << "null";
  }
  stream << ',';

  AppendJsonOptionalCoordinate(stream, "latitude_deg", state.latitude_deg);
  stream << ',';
  AppendJsonOptionalCoordinate(stream, "longitude_deg", state.longitude_deg);
  stream << ',';
  AppendJsonOptionalNumber(stream, "altitude_m", state.altitude_m);
  stream << ',';
  AppendJsonOptionalNumber(stream, "horizontal_accuracy_m", state.horizontal_accuracy_m);
  stream << ',';
  AppendJsonOptionalNumber(stream, "vertical_accuracy_m", state.vertical_accuracy_m);
  stream << ',';
  AppendJsonOptionalNumber(stream, "hdop", state.hdop);
  stream << ',';
  AppendJsonOptionalNumber(stream, "vdop", state.vdop);
  stream << ',';
  AppendJsonOptionalNumber(stream, "satellites_used", state.satellites_used);
  stream << ',';
  AppendJsonOptionalNumber(stream, "satellites_tracked", state.satellites_tracked);
  stream << ',';
  AppendJsonOptionalNumber(stream, "satellites_visible", state.satellites_visible);
  stream << ',';
  AppendJsonOptionalNumber(stream, "mean_cn0_db_hz", state.mean_cn0_db_hz);
  stream << ',';
  AppendJsonOptionalNumber(stream, "max_cn0_db_hz", state.max_cn0_db_hz);
  stream << ',';
  AppendJsonOptionalNumber(stream, "correction_age_s", state.correction_age_s);
  stream << ',';
  stream << "\"utc_date\":";
  if (state.utc_date.has_value())
  {
    stream << "{\"year\":" << state.utc_date->year << ",\"month\":"
           << static_cast<unsigned int>(state.utc_date->month) << ",\"day\":"
           << static_cast<unsigned int>(state.utc_date->day) << '}';
  }
  else { stream << "null"; }
  stream << ',';
  stream << "\"utc_time\":";
  if (state.utc_time.has_value())
  {
    stream << "{\"hour\":" << static_cast<unsigned int>(state.utc_time->hour)
           << ",\"minute\":" << static_cast<unsigned int>(state.utc_time->minute)
           << ",\"second\":" << static_cast<unsigned int>(state.utc_time->second)
           << ",\"nanosecond\":" << state.utc_time->nanosecond << '}';
  }
  else { stream << "null"; }
  stream << ',';
  AppendJsonOptionalNumber(stream, "speed_over_ground_m_s", state.speed_over_ground_m_s);
  stream << ',';
  AppendJsonOptionalNumber(stream, "course_over_ground_deg", state.course_over_ground_deg);
  stream << ',';
  AppendJsonOptionalNumber(stream, "heading_deg", state.heading_deg);
  stream << ',';
  AppendJsonOptionalBool(stream, "dual_antenna_heading", state.dual_antenna_heading);
  stream << ',';
  AppendJsonOptionalBool(stream, "dual_antenna_baseline", state.dual_antenna_baseline);
  stream << ',';
  AppendJsonOptionalNumber(stream, "baseline_azimuth_deg", state.baseline_azimuth_deg);
  stream << ',';
  AppendJsonOptionalNumber(stream, "baseline_pitch_deg", state.baseline_pitch_deg);
  stream << ',';
  AppendJsonOptionalNumber(stream, "baseline_length_m", state.baseline_length_m);
  stream << ',';
  AppendJsonOptionalEnum(stream,
                         "baseline_solution_status",
                         state.baseline_solution_status,
                         ToString);
  stream << ',';
  AppendJsonOptionalBool(stream, "interference_detected", state.interference_detected);
  stream << ',';
  AppendJsonOptionalBool(stream, "jamming_detected", state.jamming_detected);
  stream << '}';
  return stream.str();
}

}  // namespace universal_gnss_tools
