#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace universal_gnss_ntrip
{

enum class NtripClientError : std::uint8_t
{
  kNone = 0,
  kConfiguration = 1,
  kAuthentication = 2,
  kHttp = 3,
  kProtocol = 4,
  kTimeout = 5,
  kDisconnected = 6,
  kUnknown = 7,
};

struct NtripConnectionMetrics
{
  std::uint64_t bytes_received{0u};
  std::uint64_t bytes_sent{0u};
  std::uint64_t rtcm_frames_seen{0u};
  std::uint64_t rtcm_frames_received{0u};
  std::uint64_t invalid_rtcm_frames{0u};
  std::optional<std::uint16_t> last_rtcm_message_type{};
  std::optional<float> last_correction_age_s{};
  bool connected{false};
  bool request_sent{false};
  bool response_received{false};
  std::uint32_t reconnect_count{0u};
  NtripClientError last_error{NtripClientError::kNone};
};

inline void NoteReceivedBytes(NtripConnectionMetrics& metrics, const std::size_t byte_count)
{
  metrics.bytes_received += static_cast<std::uint64_t>(byte_count);
}

inline void NoteSentBytes(NtripConnectionMetrics& metrics, const std::size_t byte_count)
{
  metrics.bytes_sent += static_cast<std::uint64_t>(byte_count);
}

inline void NoteRtcmFrame(NtripConnectionMetrics& metrics,
                          const std::optional<std::uint16_t> message_type,
                          const bool valid_frame)
{
  ++metrics.rtcm_frames_seen;
  if (valid_frame)
  {
    ++metrics.rtcm_frames_received;
    metrics.last_rtcm_message_type = message_type;
  }
  else
  {
    ++metrics.invalid_rtcm_frames;
  }
}

inline void MarkConnected(NtripConnectionMetrics& metrics)
{
  metrics.connected = true;
}

inline void MarkRequestSent(NtripConnectionMetrics& metrics)
{
  metrics.request_sent = true;
}

inline void MarkResponseReceived(NtripConnectionMetrics& metrics)
{
  metrics.response_received = true;
}

inline void MarkDisconnected(NtripConnectionMetrics& metrics,
                             const NtripClientError error = NtripClientError::kDisconnected)
{
  metrics.connected = false;
  metrics.last_error = error;
}

inline void NoteReconnect(NtripConnectionMetrics& metrics)
{
  ++metrics.reconnect_count;
}

inline void ClearLastError(NtripConnectionMetrics& metrics)
{
  metrics.last_error = NtripClientError::kNone;
}

}  // namespace universal_gnss_ntrip
