#pragma once

#include <cstdint>

namespace universal_gnss_protocols
{

enum class ProtocolType : std::uint8_t
{
  kUnknown = 0,
  kNmea = 1,
  kRtcm3 = 2,
  kUbx = 3,
  kUnicore = 4,
};

}  // namespace universal_gnss_protocols
