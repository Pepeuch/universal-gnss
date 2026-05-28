#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_types.hpp"

namespace universal_gnss
{

using GnssValueFlags = GnssCapabilityFlags;

struct GnssRuntimeState
{
  std::optional<GnssTimestampNs> timestamp_ns{};

  bool fix_valid{false};
  GnssFixType fix_type{GnssFixType::kUnknown};
  std::optional<GnssRtkMode> rtk_mode{};

  std::optional<double> latitude_deg{};
  std::optional<double> longitude_deg{};
  std::optional<double> altitude_m{};

  std::optional<float> horizontal_accuracy_m{};
  std::optional<float> vertical_accuracy_m{};
  std::optional<float> hdop{};
  std::optional<float> vdop{};

  std::optional<std::uint16_t> satellites_used{};
  std::optional<std::uint16_t> satellites_visible{};
  std::optional<std::uint16_t> satellites_tracked{};

  std::optional<float> mean_cn0_db_hz{};
  std::optional<float> max_cn0_db_hz{};
  std::optional<float> correction_age_s{};

  std::optional<float> heading_deg{};
  std::optional<bool> dual_antenna_heading{};
  std::optional<bool> interference_detected{};
  std::optional<bool> jamming_detected{};

  // capability_flags declare which optional enrichment fields this runtime path
  // can provide at all.
  GnssCapabilityFlags capability_flags{0};

  // value_flags use the same GnssCapability bits and declare which optional
  // enrichment fields have a current value in this sample.
  GnssValueFlags value_flags{0};
};

constexpr bool HasCapability(const GnssRuntimeState& state, GnssCapability capability)
{
  return HasCapabilityFlag(state.capability_flags, capability);
}

inline void SetCapability(GnssRuntimeState& state, GnssCapability capability)
{
  state.capability_flags = SetCapabilityFlag(state.capability_flags, capability);
}

inline void ClearCapability(GnssRuntimeState& state, GnssCapability capability)
{
  state.capability_flags = ClearCapabilityFlag(state.capability_flags, capability);
  state.value_flags = ClearCapabilityFlag(state.value_flags, capability);
}

constexpr bool HasValueAvailable(const GnssRuntimeState& state, GnssCapability capability)
{
  return HasCapabilityFlag(state.value_flags, capability);
}

inline bool SetValueAvailable(GnssRuntimeState& state, GnssCapability capability)
{
  if (!HasCapability(state, capability))
  {
    return false;
  }

  state.value_flags = SetCapabilityFlag(state.value_flags, capability);
  return true;
}

inline void ClearValueAvailable(GnssRuntimeState& state, GnssCapability capability)
{
  state.value_flags = ClearCapabilityFlag(state.value_flags, capability);
}

constexpr bool HasValidCapabilityValueInvariant(const GnssRuntimeState& state)
{
  return (state.value_flags & ~state.capability_flags) == 0u;
}

template <typename T, typename U>
inline bool SetOptionalValue(GnssRuntimeState& state,
                             GnssCapability capability,
                             std::optional<T>& field,
                             U&& value)
{
  if (!HasCapability(state, capability))
  {
    return false;
  }

  field = static_cast<T>(std::forward<U>(value));
  return SetValueAvailable(state, capability);
}

template <typename T>
inline void ClearOptionalValue(GnssRuntimeState& state,
                               GnssCapability capability,
                               std::optional<T>& field)
{
  field.reset();
  ClearValueAvailable(state, capability);
}

inline GnssValueFlags ComputeValueFlagsFromFields(const GnssRuntimeState& state)
{
  GnssValueFlags flags = 0;

  if (HasCapability(state, GnssCapability::kRtkMode) && state.rtk_mode.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kRtkMode);
  }
  if (HasCapability(state, GnssCapability::kHorizontalAccuracy) &&
      state.horizontal_accuracy_m.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kHorizontalAccuracy);
  }
  if (HasCapability(state, GnssCapability::kVerticalAccuracy) &&
      state.vertical_accuracy_m.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kVerticalAccuracy);
  }
  if (HasCapability(state, GnssCapability::kHdop) && state.hdop.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kHdop);
  }
  if (HasCapability(state, GnssCapability::kVdop) && state.vdop.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kVdop);
  }
  if (HasCapability(state, GnssCapability::kSatellitesUsed) &&
      state.satellites_used.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kSatellitesUsed);
  }
  if (HasCapability(state, GnssCapability::kSatellitesVisible) &&
      state.satellites_visible.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kSatellitesVisible);
  }
  if (HasCapability(state, GnssCapability::kSatellitesTracked) &&
      state.satellites_tracked.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kSatellitesTracked);
  }
  if (HasCapability(state, GnssCapability::kMeanCn0) && state.mean_cn0_db_hz.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kMeanCn0);
  }
  if (HasCapability(state, GnssCapability::kMaxCn0) && state.max_cn0_db_hz.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kMaxCn0);
  }
  if (HasCapability(state, GnssCapability::kCorrectionAge) &&
      state.correction_age_s.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kCorrectionAge);
  }
  if (HasCapability(state, GnssCapability::kHeading) && state.heading_deg.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kHeading);
  }
  if (HasCapability(state, GnssCapability::kDualAntennaHeading) &&
      state.dual_antenna_heading.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kDualAntennaHeading);
  }
  if (HasCapability(state, GnssCapability::kInterferenceState) &&
      state.interference_detected.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kInterferenceState);
  }
  if (HasCapability(state, GnssCapability::kJammingState) &&
      state.jamming_detected.has_value())
  {
    flags = SetCapabilityFlag(flags, GnssCapability::kJammingState);
  }

  return flags;
}

inline void RefreshValueFlagsFromFields(GnssRuntimeState& state)
{
  state.value_flags = ComputeValueFlagsFromFields(state);
}

}  // namespace universal_gnss
