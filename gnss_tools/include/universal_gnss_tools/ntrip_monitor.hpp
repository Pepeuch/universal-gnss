#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_ntrip/ntrip_config.hpp"
#include "universal_gnss_ntrip/ntrip_metrics.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include "universal_gnss_protocols/rtcm_records.hpp"

namespace universal_gnss_tools
{

struct NtripMonitorOptions
{
  std::string host{};
  std::uint16_t port{0u};
  std::string mountpoint{};
  std::string username{};
  std::string password{};
  std::string user_agent{universal_gnss_ntrip::kDefaultNtripUserAgent};
  std::optional<double> latitude_deg{};
  std::optional<double> longitude_deg{};
  std::optional<double> altitude_m{};
  std::optional<std::uint32_t> gga_interval_s{};
  std::optional<std::size_t> max_bytes{};
  std::optional<std::uint32_t> max_seconds{};
  bool json_output{false};
  bool summary_only{false};
  std::uint32_t read_timeout_ms{1000u};
};

enum class NtripMonitorValidationError : std::uint8_t
{
  kNone = 0,
  kMissingHost = 1,
  kMissingPort = 2,
  kMissingMountpoint = 3,
  kMissingLatitude = 4,
  kMissingLongitude = 5,
  kInvalidGgaInterval = 6,
  kInvalidMaxBytes = 7,
  kInvalidMaxSeconds = 8,
  kGgaIntervalRequiresPosition = 9,
};

struct NtripMonitorValidationResult
{
  NtripMonitorValidationError error{NtripMonitorValidationError::kNone};
  std::string message{};

  bool ok() const;
};

enum class NtripMonitorStopReason : std::uint8_t
{
  kRunning = 0,
  kCompleted = 1,
  kConnectFailed = 2,
  kRequestFailed = 3,
  kGgaSendFailed = 4,
  kReadError = 5,
  kDisconnected = 6,
  kMaxBytes = 7,
  kMaxSeconds = 8,
  kInterrupted = 9,
};

struct NtripMonitorSnapshot
{
  NtripMonitorOptions options{};
  std::string client_state{"disconnected"};
  std::string response_header{};

  std::uint64_t bytes_received{0u};
  std::uint64_t bytes_sent{0u};
  std::uint64_t rtcm_frames_seen{0u};
  std::uint64_t rtcm_frames_received{0u};
  std::uint64_t invalid_rtcm_frames{0u};
  std::uint64_t gga_sent_count{0u};
  std::uint64_t gga_send_errors{0u};
  std::uint32_t reconnect_count{0u};

  bool request_sent{false};
  bool response_received{false};
  bool base_position_seen{false};
  bool base_position_1005_seen{false};
  bool base_position_1006_seen{false};
  bool glonass_bias_1230_seen{false};

  std::optional<std::uint16_t> last_rtcm_message_type{};
  std::optional<universal_gnss::GnssTimestampNs> last_gga_sent_timestamp_ns{};
  std::optional<std::int64_t> elapsed_time_ns{};

  universal_gnss_ntrip::NtripClientError last_error{
      universal_gnss_ntrip::NtripClientError::kNone};
  universal_gnss::GnssHealthSummary correction_health{};
  std::map<std::uint16_t, std::uint64_t> message_type_counts{};
  std::map<universal_gnss_protocols::RtcmConstellation, std::uint64_t>
      msm_constellation_counts{};
  NtripMonitorStopReason stop_reason{NtripMonitorStopReason::kCompleted};
};

NtripMonitorValidationResult ValidateNtripMonitorOptions(
    const NtripMonitorOptions& options);

universal_gnss_ntrip::NtripConfig BuildNtripMonitorConfig(
    const NtripMonitorOptions& options);

std::optional<universal_gnss::GnssRuntimeState> BuildNtripMonitorRuntimeState(
    const NtripMonitorOptions& options);

NtripMonitorSnapshot BuildNtripMonitorSnapshot(
    const NtripMonitorOptions& options,
    const std::string& client_state,
    const universal_gnss_ntrip::NtripConnectionMetrics& metrics,
    const universal_gnss_protocols::RtcmCorrectionMonitor& correction_monitor,
    universal_gnss::GnssHealthSummary correction_health,
    NtripMonitorStopReason stop_reason = NtripMonitorStopReason::kCompleted,
    std::optional<std::int64_t> elapsed_time_ns = std::nullopt,
    std::string response_header = {});

std::string DescribeGnssDiagnosticSeverity(
    universal_gnss::GnssDiagnosticSeverity severity);

std::string DescribeNtripClientError(
    universal_gnss_ntrip::NtripClientError error);

std::string DescribeNtripMonitorStopReason(NtripMonitorStopReason stop_reason);

std::string FormatNtripMonitorStatusLine(const NtripMonitorSnapshot& snapshot);

std::string FormatNtripMonitorSummaryText(const NtripMonitorSnapshot& snapshot);

std::string FormatNtripMonitorSummaryJson(const NtripMonitorSnapshot& snapshot);

}  // namespace universal_gnss_tools
