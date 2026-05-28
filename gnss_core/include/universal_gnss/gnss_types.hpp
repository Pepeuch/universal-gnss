#pragma once

#include <cstdint>

namespace universal_gnss
{

using GnssTimestampNs = std::int64_t;

enum class GnssFixType : std::uint8_t
{
  kUnknown = 0,
  kNoFix = 1,
  kFix = 2,
  kRtkFloat = 3,
  kRtkFixed = 4,
  kDeadReckoning = 5,
};

enum class GnssRtkMode : std::uint8_t
{
  kUnknown = 0,
  kNone = 1,
  kFloat = 2,
  kFixed = 3,
};

}  // namespace universal_gnss
