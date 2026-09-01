#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "universal_gnss/gnss_runtime_state.hpp"

namespace universal_gnss
{

class GnssRuntimeAggregator
{
public:
  GnssRuntimeAggregator() = default;

  void Reset()
  {
    state_ = GnssRuntimeState{};
    for (auto& version : field_versions_)
    {
      version = FieldVersion{};
    }
  }

  const GnssRuntimeState& state() const
  {
    return state_;
  }

  bool Merge(const GnssRuntimeState& update)
  {
    const ValueUpdateFlags effective_value_flags{
        static_cast<GnssValueFlags>(update.value_flags & update.capability_flags),
        static_cast<GnssValueFlags>(update.clear_value_flags & update.capability_flags)};

    bool applied = false;

    const GnssCapabilityFlags merged_capabilities = static_cast<GnssCapabilityFlags>(
        state_.capability_flags | update.capability_flags);
    if (merged_capabilities != state_.capability_flags)
    {
      state_.capability_flags = merged_capabilities;
      applied = true;
    }

    applied = MergeFix(update) || applied;
    applied = MergeDirectField(update,
                               FieldSlot::kLatitude,
                               GnssDirectValue::kLatitude,
                               update.latitude_deg,
                               state_.latitude_deg) ||
              applied;
    applied = MergeDirectField(update,
                               FieldSlot::kLongitude,
                               GnssDirectValue::kLongitude,
                               update.longitude_deg,
                               state_.longitude_deg) ||
              applied;
    applied = MergeDirectField(update,
                               FieldSlot::kAltitude,
                               GnssDirectValue::kAltitude,
                               update.altitude_m,
                               state_.altitude_m) ||
              applied;

    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kRtkMode,
                                   FieldSlot::kRtkMode,
                                   update.rtk_mode,
                                   state_.rtk_mode) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kHorizontalAccuracy,
                                   FieldSlot::kHorizontalAccuracy,
                                   update.horizontal_accuracy_m,
                                   state_.horizontal_accuracy_m) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kVerticalAccuracy,
                                   FieldSlot::kVerticalAccuracy,
                                   update.vertical_accuracy_m,
                                   state_.vertical_accuracy_m) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kHdop,
                                   FieldSlot::kHdop,
                                   update.hdop,
                                   state_.hdop) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kVdop,
                                   FieldSlot::kVdop,
                                   update.vdop,
                                   state_.vdop) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kSatellitesUsed,
                                   FieldSlot::kSatellitesUsed,
                                   update.satellites_used,
                                   state_.satellites_used) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kSatellitesVisible,
                                   FieldSlot::kSatellitesVisible,
                                   update.satellites_visible,
                                   state_.satellites_visible) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kSatellitesTracked,
                                   FieldSlot::kSatellitesTracked,
                                   update.satellites_tracked,
                                   state_.satellites_tracked) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kMeanCn0,
                                   FieldSlot::kMeanCn0,
                                   update.mean_cn0_db_hz,
                                   state_.mean_cn0_db_hz) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kMaxCn0,
                                   FieldSlot::kMaxCn0,
                                   update.max_cn0_db_hz,
                                   state_.max_cn0_db_hz) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kCorrectionAge,
                                   FieldSlot::kCorrectionAge,
                                   update.correction_age_s,
                                   state_.correction_age_s) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kSpeedOverGround,
                                   FieldSlot::kSpeedOverGround,
                                   update.speed_over_ground_m_s,
                                   state_.speed_over_ground_m_s) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kCourseOverGround,
                                   FieldSlot::kCourseOverGround,
                                   update.course_over_ground_deg,
                                   state_.course_over_ground_deg) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kHeading,
                                   FieldSlot::kHeading,
                                   update.heading_deg,
                                   state_.heading_deg) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kHeadingAccuracy,
                                   FieldSlot::kHeadingAccuracy,
                                   update.heading_accuracy_deg,
                                   state_.heading_accuracy_deg) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kDifferentialCorrections,
                                   FieldSlot::kDifferentialCorrections,
                                   update.differential_corrections,
                                   state_.differential_corrections) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kCorrectionsActive,
                                   FieldSlot::kCorrectionsActive,
                                   update.corrections_active,
                                   state_.corrections_active) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kDualAntennaHeading,
                                   FieldSlot::kDualAntennaHeading,
                                   update.dual_antenna_heading,
                                   state_.dual_antenna_heading) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kDualAntennaBaseline,
                                   FieldSlot::kDualAntennaBaseline,
                                   update.dual_antenna_baseline,
                                   state_.dual_antenna_baseline) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kBaselineAzimuth,
                                   FieldSlot::kBaselineAzimuth,
                                   update.baseline_azimuth_deg,
                                   state_.baseline_azimuth_deg) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kBaselinePitch,
                                   FieldSlot::kBaselinePitch,
                                   update.baseline_pitch_deg,
                                   state_.baseline_pitch_deg) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kBaselineLength,
                                   FieldSlot::kBaselineLength,
                                   update.baseline_length_m,
                                   state_.baseline_length_m) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kBaselineSolutionStatus,
                                   FieldSlot::kBaselineSolutionStatus,
                                   update.baseline_solution_status,
                                   state_.baseline_solution_status) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kInterferenceState,
                                   FieldSlot::kInterferenceState,
                                   update.interference_detected,
                                   state_.interference_detected) ||
              applied;
    applied = MergeCapabilityField(update.timestamp_ns,
                                   effective_value_flags,
                                   GnssCapability::kJammingState,
                                   FieldSlot::kJammingState,
                                   update.jamming_detected,
                                   state_.jamming_detected) ||
              applied;

    if (applied && update.timestamp_ns.has_value() &&
        (!state_.timestamp_ns.has_value() || *update.timestamp_ns >= *state_.timestamp_ns))
    {
      state_.timestamp_ns = update.timestamp_ns;
    }

    RefreshValueFlagsFromFields(state_);
    return applied;
  }

private:
  enum class FieldSlot : std::size_t
  {
    kFix = 0,
    kLatitude,
    kLongitude,
    kAltitude,
    kRtkMode,
    kHorizontalAccuracy,
    kVerticalAccuracy,
    kHdop,
    kVdop,
    kSatellitesUsed,
    kSatellitesVisible,
    kSatellitesTracked,
    kMeanCn0,
    kMaxCn0,
    kCorrectionAge,
    kSpeedOverGround,
    kCourseOverGround,
    kHeading,
    kHeadingAccuracy,
    kDifferentialCorrections,
    kCorrectionsActive,
    kDualAntennaHeading,
    kDualAntennaBaseline,
    kBaselineAzimuth,
    kBaselinePitch,
    kBaselineLength,
    kBaselineSolutionStatus,
    kInterferenceState,
    kJammingState,
    kCount,
  };

  struct FieldVersion
  {
    bool seen{false};
    std::optional<GnssTimestampNs> timestamp_ns{};
  };

  struct ValueUpdateFlags
  {
    GnssValueFlags set{0};
    GnssValueFlags clear{0};
  };

  static constexpr std::size_t kFieldCount = static_cast<std::size_t>(FieldSlot::kCount);

  static constexpr std::size_t ToIndex(FieldSlot slot)
  {
    return static_cast<std::size_t>(slot);
  }

  bool ShouldApply(FieldSlot slot, const std::optional<GnssTimestampNs>& update_timestamp_ns) const
  {
    const FieldVersion& current_version = field_versions_[ToIndex(slot)];
    if (!current_version.seen)
    {
      return true;
    }
    if (!update_timestamp_ns.has_value())
    {
      return true;
    }
    if (!current_version.timestamp_ns.has_value())
    {
      return true;
    }
    return *update_timestamp_ns >= *current_version.timestamp_ns;
  }

  void MarkApplied(FieldSlot slot, const std::optional<GnssTimestampNs>& update_timestamp_ns)
  {
    FieldVersion& current_version = field_versions_[ToIndex(slot)];
    current_version.seen = true;
    current_version.timestamp_ns = update_timestamp_ns;
  }

  bool MergeFix(const GnssRuntimeState& update)
  {
    if (update.fix_type == GnssFixType::kUnknown ||
        !ShouldApply(FieldSlot::kFix, update.timestamp_ns))
    {
      return false;
    }

    state_.fix_valid = update.fix_valid;
    state_.fix_type = update.fix_type;
    MarkApplied(FieldSlot::kFix, update.timestamp_ns);
    return true;
  }

  template <typename T>
  bool MergeDirectField(const GnssRuntimeState& update,
                        FieldSlot slot,
                        GnssDirectValue direct_value,
                        const std::optional<T>& source,
                        std::optional<T>& target)
  {
    const bool set_requested = source.has_value();
    const bool clear_requested =
        !set_requested && HasDirectValueFlag(update.clear_direct_value_flags, direct_value);
    if ((!set_requested && !clear_requested) || !ShouldApply(slot, update.timestamp_ns))
    {
      return false;
    }

    if (set_requested)
    {
      target = source;
    }
    else
    {
      target.reset();
    }
    MarkApplied(slot, update.timestamp_ns);
    return true;
  }

  template <typename T>
  bool MergeCapabilityField(const std::optional<GnssTimestampNs>& update_timestamp_ns,
                            ValueUpdateFlags effective_value_flags,
                            GnssCapability capability,
                            FieldSlot slot,
                            const std::optional<T>& source,
                            std::optional<T>& target)
  {
    const bool set_requested =
        source.has_value() && HasCapabilityFlag(effective_value_flags.set, capability);
    const bool clear_requested =
        !set_requested && HasCapabilityFlag(effective_value_flags.clear, capability);
    if ((!set_requested && !clear_requested) || !ShouldApply(slot, update_timestamp_ns))
    {
      return false;
    }

    if (set_requested)
    {
      target = source;
    }
    else
    {
      target.reset();
    }
    MarkApplied(slot, update_timestamp_ns);
    return true;
  }

  GnssRuntimeState state_{};
  std::array<FieldVersion, kFieldCount> field_versions_{};
};

}  // namespace universal_gnss
