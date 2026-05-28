#pragma once

#include <cstdint>

namespace universal_gnss_transport
{

enum class TransportStatus : std::uint8_t
{
  kOk = 0,
  kEndOfStream = 1,
  kClosed = 2,
  kError = 3,
};

inline bool IsTransportReady(const TransportStatus status)
{
  return status == TransportStatus::kOk;
}

inline bool IsTransportTerminal(const TransportStatus status)
{
  return status == TransportStatus::kEndOfStream ||
         status == TransportStatus::kClosed ||
         status == TransportStatus::kError;
}

}  // namespace universal_gnss_transport
