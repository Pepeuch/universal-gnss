#include "universal_gnss_tools/gnss_log_comparison.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>

#include "universal_gnss/gnss_types.hpp"

namespace universal_gnss_tools
{

namespace
{

constexpr double kMeanEarthRadiusM = 6371008.8;
constexpr double kPi = 3.14159265358979323846;

const char* DescribeFixType(const universal_gnss::GnssFixType fix_type)
{
  switch (fix_type)
  {
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
    case universal_gnss::GnssFixType::kUnknown:
    default:
      return "unknown";
  }
}

const char* DescribeRtkMode(const std::optional<universal_gnss::GnssRtkMode>& rtk_mode)
{
  if (!rtk_mode.has_value())
  {
    return "unknown";
  }

  switch (*rtk_mode)
  {
    case universal_gnss::GnssRtkMode::kNone:
      return "none";
    case universal_gnss::GnssRtkMode::kFloat:
      return "float";
    case universal_gnss::GnssRtkMode::kFixed:
      return "fixed";
    case universal_gnss::GnssRtkMode::kUnknown:
    default:
      return "unknown";
  }
}

template <typename T>
std::optional<double> RightMinusLeft(const std::optional<T>& left,
                                     const std::optional<T>& right)
{
  if (!left.has_value() || !right.has_value())
  {
    return std::nullopt;
  }
  return static_cast<double>(*right) - static_cast<double>(*left);
}

std::optional<double> ComputeHorizontalSeparationM(
    const universal_gnss::GnssRuntimeState& left,
    const universal_gnss::GnssRuntimeState& right)
{
  if (!left.latitude_deg.has_value() || !left.longitude_deg.has_value() ||
      !right.latitude_deg.has_value() || !right.longitude_deg.has_value())
  {
    return std::nullopt;
  }

  const double left_latitude_rad = *left.latitude_deg * kPi / 180.0;
  const double right_latitude_rad = *right.latitude_deg * kPi / 180.0;
  const double delta_latitude_rad = right_latitude_rad - left_latitude_rad;
  const double delta_longitude_rad =
      (*right.longitude_deg - *left.longitude_deg) * kPi / 180.0;
  const double sin_latitude = std::sin(delta_latitude_rad / 2.0);
  const double sin_longitude = std::sin(delta_longitude_rad / 2.0);
  const double haversine = sin_latitude * sin_latitude +
                           std::cos(left_latitude_rad) * std::cos(right_latitude_rad) *
                               sin_longitude * sin_longitude;
  const double bounded_haversine = std::max(0.0, std::min(1.0, haversine));
  return 2.0 * kMeanEarthRadiusM * std::asin(std::sqrt(bounded_haversine));
}

void WriteOptionalJsonNumber(std::ostringstream& output,
                             const char* name,
                             const std::optional<double>& value)
{
  output << '"' << name << "\":";
  if (value.has_value())
  {
    output << std::setprecision(12) << *value;
  }
  else
  {
    output << "null";
  }
}

void WriteOptionalJsonInteger(std::ostringstream& output,
                              const char* name,
                              const std::optional<std::int32_t>& value)
{
  output << '"' << name << "\":";
  if (value.has_value())
  {
    output << *value;
  }
  else
  {
    output << "null";
  }
}

void WriteReportJson(std::ostringstream& output, const GnssQualityReport& report)
{
  output << "{\"total_bytes_read\":" << report.summary.total_bytes_read
         << ",\"records_processed\":" << report.summary.records_processed
         << ",\"runtime_updates\":" << report.summary.runtime_updates
         << ",\"quality_level\":\"" << DescribeGnssQualityLevel(report.summary.quality_level)
         << "\",\"final_fix_type\":\"" << DescribeFixType(report.summary.final_fix_type)
         << "\",\"final_rtk_mode\":\"" << DescribeRtkMode(report.summary.final_rtk_mode)
         << "\",\"warning_count\":" << report.summary.warning_count
         << ",\"error_count\":" << report.summary.error_count << '}';
}

void WriteOptionalTextValue(std::ostringstream& output,
                            const char* name,
                            const std::optional<double>& value)
{
  output << ' ' << name << '=';
  if (value.has_value())
  {
    output << std::fixed << std::setprecision(6) << *value;
  }
  else
  {
    output << "unavailable";
  }
}

}  // namespace

GnssLogComparison CompareGnssQualityReports(const GnssQualityReport& left,
                                            const GnssQualityReport& right)
{
  GnssLogComparison comparison;
  comparison.left = left;
  comparison.right = right;
  comparison.summary.final_fix_type_matches =
      left.summary.final_fix_type == right.summary.final_fix_type;
  comparison.summary.final_rtk_mode_matches =
      left.summary.final_rtk_mode == right.summary.final_rtk_mode;
  comparison.summary.final_horizontal_separation_m =
      ComputeHorizontalSeparationM(left.final_state, right.final_state);
  comparison.summary.final_altitude_delta_m =
      RightMinusLeft(left.final_state.altitude_m, right.final_state.altitude_m);
  comparison.summary.horizontal_accuracy_delta_m =
      RightMinusLeft(left.final_state.horizontal_accuracy_m, right.final_state.horizontal_accuracy_m);
  if (left.final_state.satellites_used.has_value() &&
      right.final_state.satellites_used.has_value())
  {
    comparison.summary.satellites_used_delta =
        static_cast<std::int32_t>(*right.final_state.satellites_used) -
        static_cast<std::int32_t>(*left.final_state.satellites_used);
  }
  comparison.summary.mean_cn0_delta_db_hz =
      RightMinusLeft(left.final_state.mean_cn0_db_hz, right.final_state.mean_cn0_db_hz);
  comparison.summary.correction_age_delta_s =
      RightMinusLeft(left.final_state.correction_age_s, right.final_state.correction_age_s);
  return comparison;
}

GnssLogComparison BuildGnssLogComparisonBytes(const std::vector<std::uint8_t>& left,
                                               const std::vector<std::uint8_t>& right)
{
  return CompareGnssQualityReports(BuildGnssQualityReportBytes(left),
                                   BuildGnssQualityReportBytes(right));
}

std::string FormatGnssLogComparisonText(const GnssLogComparison& comparison)
{
  std::ostringstream output;
  output << "comparison schema_version=1\n";
  output << "left quality=" << DescribeGnssQualityLevel(comparison.left.summary.quality_level)
         << " fix=" << DescribeFixType(comparison.left.summary.final_fix_type)
         << " rtk=" << DescribeRtkMode(comparison.left.summary.final_rtk_mode)
         << " bytes=" << comparison.left.summary.total_bytes_read
         << " records=" << comparison.left.summary.records_processed
         << " runtime_updates=" << comparison.left.summary.runtime_updates
         << " warnings=" << comparison.left.summary.warning_count
         << " errors=" << comparison.left.summary.error_count << '\n';
  output << "right quality=" << DescribeGnssQualityLevel(comparison.right.summary.quality_level)
         << " fix=" << DescribeFixType(comparison.right.summary.final_fix_type)
         << " rtk=" << DescribeRtkMode(comparison.right.summary.final_rtk_mode)
         << " bytes=" << comparison.right.summary.total_bytes_read
         << " records=" << comparison.right.summary.records_processed
         << " runtime_updates=" << comparison.right.summary.runtime_updates
         << " warnings=" << comparison.right.summary.warning_count
         << " errors=" << comparison.right.summary.error_count << '\n';
  output << "comparison final_fix_type_matches=" <<
      (comparison.summary.final_fix_type_matches ? "true" : "false")
         << " final_rtk_mode_matches=" <<
      (comparison.summary.final_rtk_mode_matches ? "true" : "false");
  WriteOptionalTextValue(
      output, "final_horizontal_separation_m", comparison.summary.final_horizontal_separation_m);
  WriteOptionalTextValue(output, "final_altitude_right_minus_left_m",
                         comparison.summary.final_altitude_delta_m);
  WriteOptionalTextValue(output, "horizontal_accuracy_right_minus_left_m",
                         comparison.summary.horizontal_accuracy_delta_m);
  output << " satellites_used_right_minus_left=";
  if (comparison.summary.satellites_used_delta.has_value())
  {
    output << *comparison.summary.satellites_used_delta;
  }
  else
  {
    output << "unavailable";
  }
  WriteOptionalTextValue(output, "mean_cn0_right_minus_left_db_hz",
                         comparison.summary.mean_cn0_delta_db_hz);
  WriteOptionalTextValue(output, "correction_age_right_minus_left_s",
                         comparison.summary.correction_age_delta_s);
  output << '\n';
  return output.str();
}

std::string FormatGnssLogComparisonJson(const GnssLogComparison& comparison)
{
  std::ostringstream output;
  output << "{\"schema_version\":1,\"left\":";
  WriteReportJson(output, comparison.left);
  output << ",\"right\":";
  WriteReportJson(output, comparison.right);
  output << ",\"comparison\":{\"final_fix_type_matches\":"
         << (comparison.summary.final_fix_type_matches ? "true" : "false")
         << ",\"final_rtk_mode_matches\":"
         << (comparison.summary.final_rtk_mode_matches ? "true" : "false") << ',';
  WriteOptionalJsonNumber(
      output, "final_horizontal_separation_m", comparison.summary.final_horizontal_separation_m);
  output << ',';
  WriteOptionalJsonNumber(
      output, "final_altitude_right_minus_left_m", comparison.summary.final_altitude_delta_m);
  output << ',';
  WriteOptionalJsonNumber(output,
                          "horizontal_accuracy_right_minus_left_m",
                          comparison.summary.horizontal_accuracy_delta_m);
  output << ',';
  WriteOptionalJsonInteger(
      output, "satellites_used_right_minus_left", comparison.summary.satellites_used_delta);
  output << ',';
  WriteOptionalJsonNumber(
      output, "mean_cn0_right_minus_left_db_hz", comparison.summary.mean_cn0_delta_db_hz);
  output << ',';
  WriteOptionalJsonNumber(
      output, "correction_age_right_minus_left_s", comparison.summary.correction_age_delta_s);
  output << "}}\n";
  return output.str();
}

}  // namespace universal_gnss_tools
