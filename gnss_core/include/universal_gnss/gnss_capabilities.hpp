#pragma once

#include <cstdint>
#include <type_traits>

namespace universal_gnss
{

using GnssCapabilityFlags = std::uint32_t;

enum class GnssCapability : GnssCapabilityFlags
{
  kRtkMode = 1u << 0,
  kHorizontalAccuracy = 1u << 1,
  kVerticalAccuracy = 1u << 2,
  kHdop = 1u << 3,
  kVdop = 1u << 4,
  kSatellitesUsed = 1u << 5,
  kSatellitesVisible = 1u << 6,
  kSatellitesTracked = 1u << 7,
  kMeanCn0 = 1u << 8,
  kMaxCn0 = 1u << 9,
  kCorrectionAge = 1u << 10,
  kHeading = 1u << 11,
  kDualAntennaHeading = 1u << 12,
  kInterferenceState = 1u << 13,
  kJammingState = 1u << 14,
};

static_assert(std::is_same<std::underlying_type<GnssCapability>::type, GnssCapabilityFlags>::value,
              "GnssCapability must stay within a uint32_t flag set");

constexpr GnssCapabilityFlags ToFlag(GnssCapability capability)
{
  return static_cast<GnssCapabilityFlags>(capability);
}

constexpr bool HasCapabilityFlag(GnssCapabilityFlags flags, GnssCapability capability)
{
  return (flags & ToFlag(capability)) != 0u;
}

constexpr GnssCapabilityFlags SetCapabilityFlag(GnssCapabilityFlags flags, GnssCapability capability)
{
  return static_cast<GnssCapabilityFlags>(flags | ToFlag(capability));
}

constexpr GnssCapabilityFlags ClearCapabilityFlag(GnssCapabilityFlags flags, GnssCapability capability)
{
  return static_cast<GnssCapabilityFlags>(flags & ~ToFlag(capability));
}

}  // namespace universal_gnss
