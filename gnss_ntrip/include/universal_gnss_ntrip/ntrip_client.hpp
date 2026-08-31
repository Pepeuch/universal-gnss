#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_ntrip/gga_generator.hpp"
#include "universal_gnss_ntrip/gga_injector.hpp"
#include "universal_gnss_ntrip/gga_injection_policy.hpp"
#include "universal_gnss_ntrip/ntrip_config.hpp"
#include "universal_gnss_ntrip/ntrip_correction_arrival_age.hpp"
#include "universal_gnss_ntrip/ntrip_metrics.hpp"
#include "universal_gnss_ntrip/ntrip_request.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_transport/tcp_client_transport.hpp"

namespace universal_gnss_ntrip
{

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)

enum class NtripClientState : std::uint8_t
{
  kDisconnected = 0,
  kConnecting = 1,
  kConnected = 2,
  kStreaming = 3,
  kFailed = 4,
};

struct NtripClientReadResult
{
  std::size_t bytes_read{0u};
  universal_gnss_transport::TransportStatus transport_status{
      universal_gnss_transport::TransportStatus::kOk};
  universal_gnss_transport::TransportError transport_error{
      universal_gnss_transport::TransportError::kNone};
  NtripClientError client_error{NtripClientError::kNone};
};

struct NtripCorrectionFlowState
{
  bool response_accepted{false};
  std::optional<universal_gnss::GnssTimestampNs> response_accepted_timestamp_ns{};
  std::uint64_t valid_rtcm_frames{0u};
  std::optional<universal_gnss::GnssTimestampNs> first_valid_rtcm_frame_timestamp_ns{};
  std::optional<universal_gnss::GnssTimestampNs> last_valid_rtcm_frame_timestamp_ns{};
  bool operational{false};

  void Reset();
};

enum class NtripGgaSendStatus : std::uint8_t
{
  kSent = 0,
  kSkippedDisabled = 1,
  kSkippedInterval = 2,
  kSkippedPositionRequired = 3,
  kSkippedMissingPosition = 4,
  kSkippedNotStreaming = 5,
  kError = 6,
};

struct NtripGgaSendResult
{
  NtripGgaSendStatus status{NtripGgaSendStatus::kError};
  NtripClientError client_error{NtripClientError::kNone};
  std::optional<NtripGgaSendError> send_error{};
  std::optional<GgaGenerationError> generation_error{};

  bool sent() const;
  bool skipped() const;
  bool ok() const;
};

class NtripClient
{
public:
  NtripClient() = default;
  explicit NtripClient(NtripConfig config);
  NtripClient(NtripConfig config, universal_gnss_transport::TcpClientConfig tcp_config);

  void set_config(NtripConfig config);
  const NtripConfig& config() const;

  void set_tcp_config(universal_gnss_transport::TcpClientConfig config);
  const universal_gnss_transport::TcpClientConfig& tcp_config() const;

  NtripClientError Connect(
      std::optional<universal_gnss::GnssTimestampNs> timestamp_ns = std::nullopt);
  NtripClientError AdoptConnectedSocket(int fd);
  void Disconnect(NtripClientError error = NtripClientError::kNone);

  NtripClientError SendRequest(
      std::optional<universal_gnss::GnssTimestampNs> timestamp_ns = std::nullopt);
  NtripGgaSendResult SendGga(const universal_gnss::GnssRuntimeState& state,
                             universal_gnss::GnssTimestampNs now_timestamp_ns);
  NtripGgaSendResult MaybeSendGga(const universal_gnss::GnssRuntimeState& state,
                                  universal_gnss::GnssTimestampNs now_timestamp_ns);
  NtripGgaSendResult MaybeInjectGga(const universal_gnss::GnssRuntimeState& state,
                                    universal_gnss::GnssTimestampNs now_timestamp_ns);
  NtripClientReadResult Read(
      std::uint8_t* destination,
      std::size_t capacity,
      std::optional<universal_gnss_protocols::ProtocolTimestampNs> timestamp_ns = std::nullopt,
      std::vector<universal_gnss_protocols::RtcmFrame>* observed_frames = nullptr);
  std::size_t FeedRtcmMonitor(
      const std::uint8_t* data,
      std::size_t size,
      std::optional<universal_gnss_protocols::ProtocolTimestampNs> timestamp_ns = std::nullopt,
      std::vector<universal_gnss_protocols::RtcmFrame>* observed_frames = nullptr);

  universal_gnss::GnssHealthSummary BuildCorrectionHealth(
      const universal_gnss_protocols::RtcmCorrectionHealthOptions& options) const;

  NtripClientState state() const;
  bool IsConnected() const;
  const NtripCorrectionFlowState& correction_flow_state() const;
  bool IsCorrectionFlowing() const;
  std::optional<float> EstimatedCorrectionArrivalAgeS() const;
  std::optional<float> EstimatedCorrectionArrivalAgeS(
      NtripCorrectionArrivalAgeEstimator::TimePoint now) const;
  const NtripSourceIdentity& source_identity() const;
  const NtripReconnectState& reconnect_state() const;
  const GgaInjectionPolicy& gga_injection_policy() const;
  const GgaInjectorMetrics& gga_metrics() const;

  const NtripRequest& request() const;
  const std::string& response_header() const;
  const NtripConnectionMetrics& metrics() const;
  const universal_gnss_protocols::RtcmCorrectionMonitor& correction_monitor() const;

private:
  NtripClientError FailWith(
      NtripClientError error,
      std::optional<universal_gnss::GnssTimestampNs> timestamp_ns = std::nullopt);
  void ResetSessionState();
  void ResetSourceState();
  void ResetSessionMetrics();
  bool CheckCorrectionFlowTimeout(
      std::optional<universal_gnss::GnssTimestampNs> timestamp_ns,
      NtripClientReadResult& result);
  void NoteCorrectionFlowProgress(
      std::uint64_t valid_frame_count,
      std::optional<universal_gnss::GnssTimestampNs> timestamp_ns);
  NtripGgaSendResult MakeGgaSendErrorResult(
      NtripGgaSendError error,
      NtripClientError client_error = NtripClientError::kNone,
      std::optional<GgaGenerationError> generation_error = std::nullopt);
  NtripGgaSendResult RunGgaInjector(const universal_gnss::GnssRuntimeState& state,
                                    universal_gnss::GnssTimestampNs now_timestamp_ns);
  void RecordReconnectFailure(std::optional<universal_gnss::GnssTimestampNs> timestamp_ns);
  void RecordReconnectSuccess(std::optional<universal_gnss::GnssTimestampNs> timestamp_ns);

  NtripClientError HandleResponseBytes(
      const std::uint8_t* data,
      std::size_t size,
      std::uint8_t* destination,
      std::size_t capacity,
      std::size_t& payload_bytes_written,
      std::optional<universal_gnss_protocols::ProtocolTimestampNs> timestamp_ns,
      std::vector<universal_gnss_protocols::RtcmFrame>* observed_frames);

  NtripConfig config_{};
  NtripSourceIdentity source_identity_{};
  universal_gnss_transport::TcpClientConfig tcp_config_{};
  universal_gnss_transport::TcpClientTransport transport_{};
  NtripClientState state_{NtripClientState::kDisconnected};

  NtripRequest request_{};
  std::string response_buffer_{};
  std::string response_header_{};
  NtripConnectionMetrics metrics_{};
  NtripReconnectState reconnect_state_{};
  NtripCorrectionFlowState correction_flow_state_{};
  std::optional<std::chrono::steady_clock::time_point>
      response_accepted_steady_time_{};
  std::optional<std::chrono::steady_clock::time_point>
      last_valid_rtcm_frame_steady_time_{};
  NtripCorrectionArrivalAgeEstimator correction_arrival_age_estimator_{};
  GgaInjectionPolicy gga_injection_policy_{};
  GgaInjector gga_injector_{};

  universal_gnss_protocols::RtcmFrameFramer rtcm_framer_{};
  universal_gnss_protocols::RtcmCorrectionMonitor correction_monitor_{};
};

#endif

}  // namespace universal_gnss_ntrip
