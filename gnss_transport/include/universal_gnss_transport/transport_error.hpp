#pragma once

#include <cstdint>

namespace universal_gnss_transport
{

enum class TransportError : std::uint8_t
{
  kNone = 0,
  kClosed = 1,
  kInvalidArgument = 2,
  kOverflow = 3,
  kConnectFailure = 4,
  kTimeout = 5,
  kReadFailure = 6,
  kWriteFailure = 7,
  kUnsupported = 8,
  kUnknown = 9,
};

}  // namespace universal_gnss_transport
