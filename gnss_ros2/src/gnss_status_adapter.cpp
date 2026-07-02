#include "universal_gnss_ros2/gnss_status_adapter.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace universal_gnss_ros2
{

static_assert(
    std::is_same<std::underlying_type<universal_gnss::GnssCapability>::type, std::uint32_t>::value,
    "universal_gnss::GnssCapability must stay within the uint32 ROS message contract");

namespace
{

using Msg = universal_gnss_ros2::msg::GnssStatus;

constexpr std::int64_t kNanosecondsPerSecond = 1000000000LL;

template <typename T>
T QuietNaN()
{
  return std::numeric_limits<T>::quiet_NaN();
}

std::uint8_t ToMsgFixType(universal_gnss::GnssFixType fix_type)
{
  switch (fix_type)
  {
    case universal_gnss::GnssFixType::kNoFix:
      return Msg::FIX_TYPE_NO_FIX;
    case universal_gnss::GnssFixType::kFix:
      return Msg::FIX_TYPE_FIX;
    case universal_gnss::GnssFixType::kRtkFloat:
      return Msg::FIX_TYPE_RTK_FLOAT;
    case universal_gnss::GnssFixType::kRtkFixed:
      return Msg::FIX_TYPE_RTK_FIXED;
    case universal_gnss::GnssFixType::kDeadReckoning:
      return Msg::FIX_TYPE_DEAD_RECKONING;
    case universal_gnss::GnssFixType::kUnknown:
    default:
      return Msg::FIX_TYPE_UNKNOWN;
  }
}

universal_gnss::GnssFixType FromMsgFixType(const std::uint8_t fix_type)
{
  switch (fix_type)
  {
    case Msg::FIX_TYPE_NO_FIX:
      return universal_gnss::GnssFixType::kNoFix;
    case Msg::FIX_TYPE_FIX:
      return universal_gnss::GnssFixType::kFix;
    case Msg::FIX_TYPE_RTK_FLOAT:
      return universal_gnss::GnssFixType::kRtkFloat;
    case Msg::FIX_TYPE_RTK_FIXED:
      return universal_gnss::GnssFixType::kRtkFixed;
    case Msg::FIX_TYPE_DEAD_RECKONING:
      return universal_gnss::GnssFixType::kDeadReckoning;
    case Msg::FIX_TYPE_UNKNOWN:
    default:
      return universal_gnss::GnssFixType::kUnknown;
  }
}

std::uint8_t ToMsgRtkMode(universal_gnss::GnssRtkMode rtk_mode)
{
  switch (rtk_mode)
  {
    case universal_gnss::GnssRtkMode::kNone:
      return Msg::RTK_MODE_NONE;
    case universal_gnss::GnssRtkMode::kFloat:
      return Msg::RTK_MODE_FLOAT;
    case universal_gnss::GnssRtkMode::kFixed:
      return Msg::RTK_MODE_FIXED;
    case universal_gnss::GnssRtkMode::kUnknown:
    default:
      return Msg::RTK_MODE_UNKNOWN;
  }
}

universal_gnss::GnssRtkMode FromMsgRtkMode(const std::uint8_t rtk_mode)
{
  switch (rtk_mode)
  {
    case Msg::RTK_MODE_NONE:
      return universal_gnss::GnssRtkMode::kNone;
    case Msg::RTK_MODE_FLOAT:
      return universal_gnss::GnssRtkMode::kFloat;
    case Msg::RTK_MODE_FIXED:
      return universal_gnss::GnssRtkMode::kFixed;
    case Msg::RTK_MODE_UNKNOWN:
    default:
      return universal_gnss::GnssRtkMode::kUnknown;
  }
}

std::uint8_t ToMsgBaselineSolutionStatus(
    const universal_gnss::GnssBaselineSolutionStatus status)
{
  switch (status)
  {
    case universal_gnss::GnssBaselineSolutionStatus::kComputed:
      return Msg::BASELINE_STATUS_COMPUTED;
    case universal_gnss::GnssBaselineSolutionStatus::kNotSolved:
      return Msg::BASELINE_STATUS_NOT_SOLVED;
    case universal_gnss::GnssBaselineSolutionStatus::kInsufficientObservations:
      return Msg::BASELINE_STATUS_INSUFFICIENT_OBSERVATIONS;
    case universal_gnss::GnssBaselineSolutionStatus::kNoConvergence:
      return Msg::BASELINE_STATUS_NO_CONVERGENCE;
    case universal_gnss::GnssBaselineSolutionStatus::kOutOfTolerance:
      return Msg::BASELINE_STATUS_OUT_OF_TOLERANCE;
    case universal_gnss::GnssBaselineSolutionStatus::kCovarianceTraceExceeded:
      return Msg::BASELINE_STATUS_COVARIANCE_TRACE_EXCEEDED;
    case universal_gnss::GnssBaselineSolutionStatus::kNotConfigured:
      return Msg::BASELINE_STATUS_NOT_CONFIGURED;
    case universal_gnss::GnssBaselineSolutionStatus::kUnknown:
    default:
      return Msg::BASELINE_STATUS_UNKNOWN;
  }
}

universal_gnss::GnssBaselineSolutionStatus FromMsgBaselineSolutionStatus(
    const std::uint8_t status)
{
  switch (status)
  {
    case Msg::BASELINE_STATUS_COMPUTED:
      return universal_gnss::GnssBaselineSolutionStatus::kComputed;
    case Msg::BASELINE_STATUS_NOT_SOLVED:
      return universal_gnss::GnssBaselineSolutionStatus::kNotSolved;
    case Msg::BASELINE_STATUS_INSUFFICIENT_OBSERVATIONS:
      return universal_gnss::GnssBaselineSolutionStatus::kInsufficientObservations;
    case Msg::BASELINE_STATUS_NO_CONVERGENCE:
      return universal_gnss::GnssBaselineSolutionStatus::kNoConvergence;
    case Msg::BASELINE_STATUS_OUT_OF_TOLERANCE:
      return universal_gnss::GnssBaselineSolutionStatus::kOutOfTolerance;
    case Msg::BASELINE_STATUS_COVARIANCE_TRACE_EXCEEDED:
      return universal_gnss::GnssBaselineSolutionStatus::kCovarianceTraceExceeded;
    case Msg::BASELINE_STATUS_NOT_CONFIGURED:
      return universal_gnss::GnssBaselineSolutionStatus::kNotConfigured;
    case Msg::BASELINE_STATUS_UNKNOWN:
    default:
      return universal_gnss::GnssBaselineSolutionStatus::kUnknown;
  }
}

template <typename T>
void AssignOptionalWithNaN(T& destination, const std::optional<T>& source)
{
  destination = source.has_value() ? *source : QuietNaN<T>();
}

template <typename T>
void AssignFlaggedOptional(T& destination,
                           const std::optional<T>& source,
                           bool value_available)
{
  destination = (value_available && source.has_value()) ? *source : QuietNaN<T>();
}

void AssignFlaggedOptional(std::uint16_t& destination,
                           const std::optional<std::uint16_t>& source,
                           bool value_available)
{
  destination = (value_available && source.has_value()) ? *source : 0u;
}

void AssignFlaggedOptional(bool& destination,
                           const std::optional<bool>& source,
                           bool value_available)
{
  destination = value_available && source.has_value() ? *source : false;
}

std::optional<universal_gnss::GnssTimestampNs> FromRosTime(
    const builtin_interfaces::msg::Time& stamp)
{
  if (stamp.sec == 0 && stamp.nanosec == 0u)
  {
    return std::nullopt;
  }

  return static_cast<universal_gnss::GnssTimestampNs>(stamp.sec) * kNanosecondsPerSecond +
         static_cast<universal_gnss::GnssTimestampNs>(stamp.nanosec);
}

template <typename T>
std::optional<T> OptionalFromFinite(const T value)
{
  return std::isfinite(value) ? std::optional<T>(value) : std::nullopt;
}

template <typename T>
void AssignOptionalField(std::optional<T>& destination,
                         const T value,
                         const bool value_available)
{
  if (!value_available)
  {
    destination.reset();
    return;
  }

  destination = value;
}

void AssignOptionalField(std::optional<float>& destination,
                         const float value,
                         const bool value_available)
{
  if (!value_available || !std::isfinite(value))
  {
    destination.reset();
    return;
  }

  destination = value;
}

void AssignOptionalField(std::optional<bool>& destination,
                         const bool value,
                         const bool value_available)
{
  if (!value_available)
  {
    destination.reset();
    return;
  }

  destination = value;
}

}  // namespace

builtin_interfaces::msg::Time ToRosTime(
    const std::optional<universal_gnss::GnssTimestampNs>& timestamp_ns)
{
  builtin_interfaces::msg::Time stamp;
  if (!timestamp_ns.has_value())
  {
    return stamp;
  }

  std::int64_t seconds = *timestamp_ns / kNanosecondsPerSecond;
  std::int64_t nanoseconds = *timestamp_ns % kNanosecondsPerSecond;

  if (nanoseconds < 0)
  {
    nanoseconds += kNanosecondsPerSecond;
    --seconds;
  }

  if (seconds < std::numeric_limits<std::int32_t>::min())
  {
    stamp.sec = std::numeric_limits<std::int32_t>::min();
    stamp.nanosec = 0u;
    return stamp;
  }

  if (seconds > std::numeric_limits<std::int32_t>::max())
  {
    stamp.sec = std::numeric_limits<std::int32_t>::max();
    stamp.nanosec = 999999999u;
    return stamp;
  }

  stamp.sec = static_cast<std::int32_t>(seconds);
  stamp.nanosec = static_cast<std::uint32_t>(nanoseconds);
  return stamp;
}

bool HasValidCapabilityValueInvariant(const universal_gnss_ros2::msg::GnssStatus& message)
{
  return (message.value_flags & ~message.capability_flags) == 0u;
}

universal_gnss_ros2::msg::GnssStatus ToGnssStatusMessage(
    const universal_gnss::GnssRuntimeState& state)
{
  const auto available_from_fields = universal_gnss::ComputeValueFlagsFromFields(state);

  assert((state.value_flags & ~state.capability_flags) == 0u &&
         "value_flags must never contain bits not present in capability_flags");
  assert((state.value_flags & ~available_from_fields) == 0u &&
         "value_flags must only be set when the corresponding optional field is present");

  Msg message;
  message.stamp = ToRosTime(state.timestamp_ns);
  message.fix_valid = state.fix_valid;
  message.fix_type = ToMsgFixType(state.fix_type);
  message.rtk_mode = Msg::RTK_MODE_UNKNOWN;
  message.capability_flags = state.capability_flags;
  message.value_flags = state.value_flags & state.capability_flags & available_from_fields;

  AssignOptionalWithNaN(message.latitude_deg, state.latitude_deg);
  AssignOptionalWithNaN(message.longitude_deg, state.longitude_deg);
  AssignOptionalWithNaN(message.altitude_m, state.altitude_m);

  const bool has_rtk_mode =
      (message.value_flags & Msg::CAP_RTK_MODE) != 0u && state.rtk_mode.has_value();
  const bool has_horizontal_accuracy =
      (message.value_flags & Msg::CAP_HORIZONTAL_ACCURACY) != 0u;
  const bool has_vertical_accuracy =
      (message.value_flags & Msg::CAP_VERTICAL_ACCURACY) != 0u;
  const bool has_hdop = (message.value_flags & Msg::CAP_HDOP) != 0u;
  const bool has_vdop = (message.value_flags & Msg::CAP_VDOP) != 0u;
  const bool has_satellites_used = (message.value_flags & Msg::CAP_SATELLITES_USED) != 0u;
  const bool has_satellites_visible = (message.value_flags & Msg::CAP_SATELLITES_VISIBLE) != 0u;
  const bool has_satellites_tracked = (message.value_flags & Msg::CAP_SATELLITES_TRACKED) != 0u;
  const bool has_mean_cn0 = (message.value_flags & Msg::CAP_MEAN_CN0) != 0u;
  const bool has_max_cn0 = (message.value_flags & Msg::CAP_MAX_CN0) != 0u;
  const bool has_correction_age = (message.value_flags & Msg::CAP_CORRECTION_AGE) != 0u;
  const bool has_heading = (message.value_flags & Msg::CAP_HEADING) != 0u;
  const bool has_heading_accuracy = (message.value_flags & Msg::CAP_HEADING_ACCURACY) != 0u;
  const bool has_differential_corrections =
      (message.value_flags & Msg::CAP_DIFFERENTIAL_CORRECTIONS) != 0u;
  const bool has_corrections_active =
      (message.value_flags & Msg::CAP_CORRECTIONS_ACTIVE) != 0u;
  const bool has_dual_antenna_heading =
      (message.value_flags & Msg::CAP_DUAL_ANTENNA_HEADING) != 0u;
  const bool has_dual_antenna_baseline =
      (message.value_flags & Msg::CAP_DUAL_ANTENNA_BASELINE) != 0u;
  const bool has_baseline_azimuth =
      (message.value_flags & Msg::CAP_BASELINE_AZIMUTH) != 0u;
  const bool has_baseline_pitch = (message.value_flags & Msg::CAP_BASELINE_PITCH) != 0u;
  const bool has_baseline_length = (message.value_flags & Msg::CAP_BASELINE_LENGTH) != 0u;
  const bool has_baseline_solution_status =
      (message.value_flags & Msg::CAP_BASELINE_SOLUTION_STATUS) != 0u;
  const bool has_interference_state =
      (message.value_flags & Msg::CAP_INTERFERENCE_STATE) != 0u;
  const bool has_jamming_state = (message.value_flags & Msg::CAP_JAMMING_STATE) != 0u;

  if (has_rtk_mode)
  {
    message.rtk_mode = ToMsgRtkMode(*state.rtk_mode);
  }

  AssignFlaggedOptional(
      message.horizontal_accuracy_m, state.horizontal_accuracy_m, has_horizontal_accuracy);
  AssignFlaggedOptional(
      message.vertical_accuracy_m, state.vertical_accuracy_m, has_vertical_accuracy);
  AssignFlaggedOptional(message.hdop, state.hdop, has_hdop);
  AssignFlaggedOptional(message.vdop, state.vdop, has_vdop);
  AssignFlaggedOptional(message.satellites_used, state.satellites_used, has_satellites_used);
  AssignFlaggedOptional(
      message.satellites_visible, state.satellites_visible, has_satellites_visible);
  AssignFlaggedOptional(
      message.satellites_tracked, state.satellites_tracked, has_satellites_tracked);
  AssignFlaggedOptional(message.mean_cn0_db_hz, state.mean_cn0_db_hz, has_mean_cn0);
  AssignFlaggedOptional(message.max_cn0_db_hz, state.max_cn0_db_hz, has_max_cn0);
  AssignFlaggedOptional(message.correction_age_s, state.correction_age_s, has_correction_age);
  AssignFlaggedOptional(message.heading_deg, state.heading_deg, has_heading);
  AssignFlaggedOptional(
      message.heading_accuracy_deg, state.heading_accuracy_deg, has_heading_accuracy);
  AssignFlaggedOptional(message.differential_corrections,
                        state.differential_corrections,
                        has_differential_corrections);
  AssignFlaggedOptional(
      message.corrections_active, state.corrections_active, has_corrections_active);
  AssignFlaggedOptional(
      message.dual_antenna_heading, state.dual_antenna_heading, has_dual_antenna_heading);
  AssignFlaggedOptional(message.dual_antenna_baseline,
                        state.dual_antenna_baseline,
                        has_dual_antenna_baseline);
  AssignFlaggedOptional(
      message.baseline_azimuth_deg, state.baseline_azimuth_deg, has_baseline_azimuth);
  AssignFlaggedOptional(
      message.baseline_pitch_deg, state.baseline_pitch_deg, has_baseline_pitch);
  AssignFlaggedOptional(
      message.baseline_length_m, state.baseline_length_m, has_baseline_length);
  if (has_baseline_solution_status && state.baseline_solution_status.has_value())
  {
    message.baseline_solution_status = ToMsgBaselineSolutionStatus(*state.baseline_solution_status);
  }
  AssignFlaggedOptional(
      message.interference_detected, state.interference_detected, has_interference_state);
  AssignFlaggedOptional(message.jamming_detected, state.jamming_detected, has_jamming_state);

  return message;
}

universal_gnss::GnssRuntimeState FromGnssStatusMessage(
    const universal_gnss_ros2::msg::GnssStatus& message)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = FromRosTime(message.stamp);
  state.fix_valid = message.fix_valid;
  state.fix_type = FromMsgFixType(message.fix_type);

  state.capability_flags = message.capability_flags;
  state.value_flags = message.value_flags & message.capability_flags;

  state.latitude_deg = OptionalFromFinite(message.latitude_deg);
  state.longitude_deg = OptionalFromFinite(message.longitude_deg);
  state.altitude_m = OptionalFromFinite(message.altitude_m);

  if ((state.value_flags & Msg::CAP_RTK_MODE) != 0u)
  {
    state.rtk_mode = FromMsgRtkMode(message.rtk_mode);
  }

  AssignOptionalField(state.horizontal_accuracy_m,
                      message.horizontal_accuracy_m,
                      (state.value_flags & Msg::CAP_HORIZONTAL_ACCURACY) != 0u);
  AssignOptionalField(state.vertical_accuracy_m,
                      message.vertical_accuracy_m,
                      (state.value_flags & Msg::CAP_VERTICAL_ACCURACY) != 0u);
  AssignOptionalField(
      state.hdop, message.hdop, (state.value_flags & Msg::CAP_HDOP) != 0u);
  AssignOptionalField(
      state.vdop, message.vdop, (state.value_flags & Msg::CAP_VDOP) != 0u);
  AssignOptionalField(state.satellites_used,
                      message.satellites_used,
                      (state.value_flags & Msg::CAP_SATELLITES_USED) != 0u);
  AssignOptionalField(state.satellites_visible,
                      message.satellites_visible,
                      (state.value_flags & Msg::CAP_SATELLITES_VISIBLE) != 0u);
  AssignOptionalField(state.satellites_tracked,
                      message.satellites_tracked,
                      (state.value_flags & Msg::CAP_SATELLITES_TRACKED) != 0u);
  AssignOptionalField(state.mean_cn0_db_hz,
                      message.mean_cn0_db_hz,
                      (state.value_flags & Msg::CAP_MEAN_CN0) != 0u);
  AssignOptionalField(state.max_cn0_db_hz,
                      message.max_cn0_db_hz,
                      (state.value_flags & Msg::CAP_MAX_CN0) != 0u);
  AssignOptionalField(state.correction_age_s,
                      message.correction_age_s,
                      (state.value_flags & Msg::CAP_CORRECTION_AGE) != 0u);
  AssignOptionalField(state.heading_deg,
                      message.heading_deg,
                      (state.value_flags & Msg::CAP_HEADING) != 0u);
  AssignOptionalField(state.heading_accuracy_deg,
                      message.heading_accuracy_deg,
                      (state.value_flags & Msg::CAP_HEADING_ACCURACY) != 0u);
  AssignOptionalField(state.differential_corrections,
                      message.differential_corrections,
                      (state.value_flags & Msg::CAP_DIFFERENTIAL_CORRECTIONS) != 0u);
  AssignOptionalField(state.corrections_active,
                      message.corrections_active,
                      (state.value_flags & Msg::CAP_CORRECTIONS_ACTIVE) != 0u);
  AssignOptionalField(state.dual_antenna_heading,
                      message.dual_antenna_heading,
                      (state.value_flags & Msg::CAP_DUAL_ANTENNA_HEADING) != 0u);
  AssignOptionalField(state.dual_antenna_baseline,
                      message.dual_antenna_baseline,
                      (state.value_flags & Msg::CAP_DUAL_ANTENNA_BASELINE) != 0u);
  AssignOptionalField(state.baseline_azimuth_deg,
                      message.baseline_azimuth_deg,
                      (state.value_flags & Msg::CAP_BASELINE_AZIMUTH) != 0u);
  AssignOptionalField(state.baseline_pitch_deg,
                      message.baseline_pitch_deg,
                      (state.value_flags & Msg::CAP_BASELINE_PITCH) != 0u);
  AssignOptionalField(state.baseline_length_m,
                      message.baseline_length_m,
                      (state.value_flags & Msg::CAP_BASELINE_LENGTH) != 0u);
  if ((state.value_flags & Msg::CAP_BASELINE_SOLUTION_STATUS) != 0u)
  {
    state.baseline_solution_status =
        FromMsgBaselineSolutionStatus(message.baseline_solution_status);
  }
  AssignOptionalField(state.interference_detected,
                      message.interference_detected,
                      (state.value_flags & Msg::CAP_INTERFERENCE_STATE) != 0u);
  AssignOptionalField(state.jamming_detected,
                      message.jamming_detected,
                      (state.value_flags & Msg::CAP_JAMMING_STATE) != 0u);
  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

}  // namespace universal_gnss_ros2
