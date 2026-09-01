#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_tools/gnss_quality_report.hpp"

namespace universal_gnss_tools
{

struct GnssLogComparisonSummary
{
  bool final_fix_type_matches{true};
  bool final_rtk_mode_matches{true};
  std::optional<double> final_horizontal_separation_m{};
  std::optional<double> final_altitude_delta_m{};
  std::optional<double> horizontal_accuracy_delta_m{};
  std::optional<std::int32_t> satellites_used_delta{};
  std::optional<double> mean_cn0_delta_db_hz{};
  std::optional<double> correction_age_delta_s{};
};

struct GnssLogComparison
{
  GnssQualityReport left{};
  GnssQualityReport right{};
  GnssLogComparisonSummary summary{};
};

GnssLogComparison CompareGnssQualityReports(const GnssQualityReport& left,
                                            const GnssQualityReport& right);

GnssLogComparison BuildGnssLogComparisonBytes(const std::vector<std::uint8_t>& left,
                                               const std::vector<std::uint8_t>& right);

std::string FormatGnssLogComparisonText(const GnssLogComparison& comparison);

std::string FormatGnssLogComparisonJson(const GnssLogComparison& comparison);

}  // namespace universal_gnss_tools
