#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include "universal_gnss_protocols/rtcm_records.hpp"

namespace universal_gnss_tools
{

enum class GnssQualityLevel : std::uint8_t
{
  kUnknown = 0,
  kPoor = 1,
  kUsable = 2,
  kGood = 3,
  kRtkFloat = 4,
  kRtkFixed = 5,
};

struct GnssReceiverRtcmDiagnosticsSummary
{
  std::size_t events_observed{0};
  std::size_t accepted_messages{0};
  std::size_t not_used_messages{0};
  std::size_t crc_failed_messages{0};
};

struct GnssQualityReportRtcmSummary
{
  std::size_t total_frames{0};
  std::size_t valid_frames{0};
  std::size_t invalid_frames{0};
  std::map<std::uint16_t, std::size_t> message_type_counts{};
  std::map<universal_gnss_protocols::RtcmConstellation, std::size_t> msm_constellation_counts{};
  GnssReceiverRtcmDiagnosticsSummary receiver_side{};
  universal_gnss_protocols::RtcmSemanticObservations semantic_observations{};
  std::optional<universal_gnss_protocols::RtcmBaseStationArpRecord> last_base_station_arp{};
};

struct GnssQualityReportSummary
{
  std::size_t total_bytes_read{0};
  std::size_t records_processed{0};
  std::size_t runtime_updates{0};
  std::map<std::string, std::size_t> counts_by_protocol{};

  GnssQualityLevel quality_level{GnssQualityLevel::kUnknown};
  universal_gnss::GnssFixType final_fix_type{universal_gnss::GnssFixType::kUnknown};
  std::optional<universal_gnss::GnssRtkMode> final_rtk_mode{};

  std::optional<float> best_horizontal_accuracy_m{};
  std::optional<float> latest_horizontal_accuracy_m{};
  std::optional<float> latest_vertical_accuracy_m{};
  std::optional<float> latest_hdop{};
  std::optional<float> latest_vdop{};
  std::optional<std::uint16_t> satellites_used{};
  std::optional<std::uint16_t> satellites_tracked{};
  std::optional<std::uint16_t> satellites_visible{};
  std::optional<float> mean_cn0_db_hz{};
  std::optional<float> max_cn0_db_hz{};

  std::size_t warning_count{0};
  std::size_t error_count{0};
};

struct GnssQualityReport
{
  GnssQualityReportSummary summary{};
  universal_gnss::GnssRuntimeState final_state{};
  GnssQualityReportRtcmSummary rtcm{};
  universal_gnss::GnssDiagnosticEvents diagnostics{};
};

const char* DescribeGnssQualityLevel(GnssQualityLevel level);

GnssQualityReport BuildGnssQualityReportBytes(const std::vector<std::uint8_t>& bytes);

GnssQualityReport BuildGnssQualityReportStream(std::istream& input);

std::string FormatGnssQualityReportText(
    const GnssQualityReport& report,
    bool summary_only = false);

std::string FormatGnssQualityReportJson(
    const GnssQualityReport& report,
    bool summary_only = false);

}  // namespace universal_gnss_tools
