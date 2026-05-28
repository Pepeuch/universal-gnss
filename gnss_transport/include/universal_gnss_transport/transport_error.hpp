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
  kReadFailure = 4,
  kWriteFailure = 5,
  kUnsupported = 6,
  kUnknown = 7,
};

}  // namespace universal_gnss_transport
