#pragma once

#include <cstddef>
#include <cstdint>

#include "universal_gnss_transport/transport_error.hpp"

namespace universal_gnss_transport
{

struct TransportMetrics
{
  std::uint64_t bytes_read{0u};
  std::uint64_t bytes_written{0u};
  std::uint64_t read_errors{0u};
  std::uint64_t write_errors{0u};
  std::uint32_t reconnect_count{0u};
  TransportError last_error{TransportError::kNone};
};

inline void NoteReadBytes(TransportMetrics& metrics, const std::size_t byte_count)
{
  metrics.bytes_read += static_cast<std::uint64_t>(byte_count);
}

inline void NoteWrittenBytes(TransportMetrics& metrics, const std::size_t byte_count)
{
  metrics.bytes_written += static_cast<std::uint64_t>(byte_count);
}

inline void NoteReadError(TransportMetrics& metrics, const TransportError error)
{
  ++metrics.read_errors;
  metrics.last_error = error;
}

inline void NoteWriteError(TransportMetrics& metrics, const TransportError error)
{
  ++metrics.write_errors;
  metrics.last_error = error;
}

inline void NoteReconnect(TransportMetrics& metrics)
{
  ++metrics.reconnect_count;
}

inline void ClearLastTransportError(TransportMetrics& metrics)
{
  metrics.last_error = TransportError::kNone;
}

}  // namespace universal_gnss_transport
