#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss_ntrip/ntrip_config.hpp"
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

  NtripClientError Connect();
  NtripClientError AdoptConnectedSocket(int fd);
  void Disconnect(NtripClientError error = NtripClientError::kNone);

  NtripClientError SendRequest();
  NtripClientReadResult Read(
      std::uint8_t* destination,
      std::size_t capacity,
      std::optional<universal_gnss_protocols::ProtocolTimestampNs> timestamp_ns = std::nullopt);
  std::size_t FeedRtcmMonitor(
      const std::uint8_t* data,
      std::size_t size,
      std::optional<universal_gnss_protocols::ProtocolTimestampNs> timestamp_ns = std::nullopt);

  universal_gnss::GnssHealthSummary BuildCorrectionHealth(
      const universal_gnss_protocols::RtcmCorrectionHealthOptions& options) const;

  NtripClientState state() const;
  bool IsConnected() const;
  const NtripReconnectState& reconnect_state() const;

  const NtripRequest& request() const;
  const std::string& response_header() const;
  const NtripConnectionMetrics& metrics() const;
  const universal_gnss_protocols::RtcmCorrectionMonitor& correction_monitor() const;

private:
  NtripClientError ConnectWithTransport(
      const universal_gnss_transport::TcpClientConfig& transport_config);
  NtripClientError FailWith(
      NtripClientError error,
      std::optional<universal_gnss::GnssTimestampNs> timestamp_ns = std::nullopt);
  void ResetSessionState();
  void ResetSessionMetrics();
  void RecordReconnectFailure(std::optional<universal_gnss::GnssTimestampNs> timestamp_ns);
  void RecordReconnectSuccess(std::optional<universal_gnss::GnssTimestampNs> timestamp_ns);

  NtripClientError HandleResponseBytes(
      const std::uint8_t* data,
      std::size_t size,
      std::uint8_t* destination,
      std::size_t capacity,
      std::size_t& payload_bytes_written,
      std::optional<universal_gnss_protocols::ProtocolTimestampNs> timestamp_ns);

  NtripConfig config_{};
  universal_gnss_transport::TcpClientConfig tcp_config_{};
  universal_gnss_transport::TcpClientTransport transport_{};
  NtripClientState state_{NtripClientState::kDisconnected};

  NtripRequest request_{};
  std::string response_buffer_{};
  std::string response_header_{};
  NtripConnectionMetrics metrics_{};
  NtripReconnectState reconnect_state_{};

  universal_gnss_protocols::RtcmFrameFramer rtcm_framer_{};
  universal_gnss_protocols::RtcmCorrectionMonitor correction_monitor_{};
};

#endif

}  // namespace universal_gnss_ntrip
