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
  const bool has_dual_antenna_heading =
      (message.value_flags & Msg::CAP_DUAL_ANTENNA_HEADING) != 0u;
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
      message.dual_antenna_heading, state.dual_antenna_heading, has_dual_antenna_heading);
  AssignFlaggedOptional(
      message.interference_detected, state.interference_detected, has_interference_state);
  AssignFlaggedOptional(message.jamming_detected, state.jamming_detected, has_jamming_state);

  return message;
}

}  // namespace universal_gnss_ros2
