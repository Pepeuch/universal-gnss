#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_ros2/gnss_status_adapter.hpp"

namespace
{

using Msg = universal_gnss_ros2::msg::GnssStatus;

TEST(GnssStatusAdapterTest, CapabilityBitsMatchPublicMessageContract)
{
  using universal_gnss::GnssCapability;

  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kRtkMode), Msg::CAP_RTK_MODE);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kHorizontalAccuracy),
            Msg::CAP_HORIZONTAL_ACCURACY);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kVerticalAccuracy),
            Msg::CAP_VERTICAL_ACCURACY);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kHdop), Msg::CAP_HDOP);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kVdop), Msg::CAP_VDOP);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kSatellitesUsed),
            Msg::CAP_SATELLITES_USED);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kSatellitesVisible),
            Msg::CAP_SATELLITES_VISIBLE);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kSatellitesTracked),
            Msg::CAP_SATELLITES_TRACKED);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kMeanCn0), Msg::CAP_MEAN_CN0);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kMaxCn0), Msg::CAP_MAX_CN0);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kCorrectionAge),
            Msg::CAP_CORRECTION_AGE);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kHeading), Msg::CAP_HEADING);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kDualAntennaHeading),
            Msg::CAP_DUAL_ANTENNA_HEADING);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kInterferenceState),
            Msg::CAP_INTERFERENCE_STATE);
  EXPECT_EQ(static_cast<std::uint32_t>(GnssCapability::kJammingState),
            Msg::CAP_JAMMING_STATE);
}

TEST(GnssStatusAdapterTest, SanitizesInvalidValueFlagsToPublicInvariant)
{
  universal_gnss::GnssRuntimeState state;
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHdop);
  state.hdop = 0.8f;
  state.value_flags = static_cast<std::uint32_t>(Msg::CAP_HDOP | Msg::CAP_VDOP);

#if !defined(NDEBUG)
  EXPECT_DEATH(
      {
        const auto msg = universal_gnss_ros2::ToGnssStatusMessage(state);
        static_cast<void>(msg);
      },
      "value_flags must never contain bits not present in capability_flags");
#else
  const auto msg = universal_gnss_ros2::ToGnssStatusMessage(state);
  EXPECT_TRUE(universal_gnss_ros2::HasValidCapabilityValueInvariant(msg));
  EXPECT_EQ(msg.value_flags, Msg::CAP_HDOP);
#endif
}

TEST(GnssStatusAdapterTest, MapsMinimalStateWithoutInventingRichFields)
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = 48.123456;
  state.longitude_deg = 2.345678;
  state.altitude_m = 123.4;

  const auto msg = universal_gnss_ros2::ToGnssStatusMessage(state);

  EXPECT_TRUE(msg.fix_valid);
  EXPECT_EQ(msg.fix_type, Msg::FIX_TYPE_FIX);
  EXPECT_EQ(msg.rtk_mode, Msg::RTK_MODE_UNKNOWN);
  EXPECT_DOUBLE_EQ(msg.latitude_deg, 48.123456);
  EXPECT_DOUBLE_EQ(msg.longitude_deg, 2.345678);
  EXPECT_DOUBLE_EQ(msg.altitude_m, 123.4);
  EXPECT_EQ(msg.capability_flags, 0u);
  EXPECT_EQ(msg.value_flags, 0u);
  EXPECT_TRUE(std::isnan(msg.horizontal_accuracy_m));
  EXPECT_TRUE(std::isnan(msg.hdop));
  EXPECT_EQ(msg.satellites_used, 0u);
  EXPECT_FALSE(msg.dual_antenna_heading);
  EXPECT_TRUE(universal_gnss_ros2::HasValidCapabilityValueInvariant(msg));
}

TEST(GnssStatusAdapterTest, MapsOptionalFieldsToCapabilityAndValueFlags)
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kRtkFloat;
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kRtkMode);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHdop);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesUsed);
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kHdop, state.hdop, 1.4f));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kSatellitesUsed, state.satellites_used, 9u));

  const auto msg = universal_gnss_ros2::ToGnssStatusMessage(state);

  EXPECT_EQ(msg.fix_type, Msg::FIX_TYPE_RTK_FLOAT);
  EXPECT_EQ(msg.rtk_mode, Msg::RTK_MODE_UNKNOWN);
  EXPECT_NE(msg.capability_flags & Msg::CAP_RTK_MODE, 0u);
  EXPECT_NE(msg.capability_flags & Msg::CAP_HDOP, 0u);
  EXPECT_NE(msg.capability_flags & Msg::CAP_SATELLITES_USED, 0u);
  EXPECT_EQ(msg.value_flags & Msg::CAP_RTK_MODE, 0u);
  EXPECT_NE(msg.value_flags & Msg::CAP_HDOP, 0u);
  EXPECT_NE(msg.value_flags & Msg::CAP_SATELLITES_USED, 0u);
  EXPECT_FLOAT_EQ(msg.hdop, 1.4f);
  EXPECT_EQ(msg.satellites_used, 9u);
}

TEST(GnssStatusAdapterTest, MapsRicherRtkStateWithExpectedFields)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = 1234567890LL;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kRtkFixed;
  state.latitude_deg = 48.0;
  state.longitude_deg = 2.0;
  state.altitude_m = 100.0;

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kRtkMode);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHorizontalAccuracy);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesVisible);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesTracked);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kMeanCn0);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kMaxCn0);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHeading);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kDualAntennaHeading);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kInterferenceState);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kJammingState);

  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kRtkMode, state.rtk_mode,
      universal_gnss::GnssRtkMode::kFixed));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kHorizontalAccuracy, state.horizontal_accuracy_m,
      0.02f));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kSatellitesVisible, state.satellites_visible, 24u));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kSatellitesTracked, state.satellites_tracked, 19u));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kMeanCn0, state.mean_cn0_db_hz, 41.5f));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kMaxCn0, state.max_cn0_db_hz, 51.0f));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kHeading, state.heading_deg, 182.0f));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kDualAntennaHeading, state.dual_antenna_heading,
      true));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kInterferenceState, state.interference_detected,
      false));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kJammingState, state.jamming_detected, true));

  const auto msg = universal_gnss_ros2::ToGnssStatusMessage(state);

  EXPECT_EQ(msg.stamp.sec, 1);
  EXPECT_EQ(msg.stamp.nanosec, 234567890u);
  EXPECT_EQ(msg.fix_type, Msg::FIX_TYPE_RTK_FIXED);
  EXPECT_EQ(msg.rtk_mode, Msg::RTK_MODE_FIXED);
  EXPECT_FLOAT_EQ(msg.horizontal_accuracy_m, 0.02f);
  EXPECT_EQ(msg.satellites_visible, 24u);
  EXPECT_EQ(msg.satellites_tracked, 19u);
  EXPECT_FLOAT_EQ(msg.mean_cn0_db_hz, 41.5f);
  EXPECT_FLOAT_EQ(msg.max_cn0_db_hz, 51.0f);
  EXPECT_FLOAT_EQ(msg.heading_deg, 182.0f);
  EXPECT_TRUE(msg.dual_antenna_heading);
  EXPECT_FALSE(msg.interference_detected);
  EXPECT_TRUE(msg.jamming_detected);
  EXPECT_TRUE(universal_gnss_ros2::HasValidCapabilityValueInvariant(msg));
}

TEST(GnssStatusAdapterTest, DefaultUnknownStateMapsSafely)
{
  const universal_gnss::GnssRuntimeState state;
  const auto msg = universal_gnss_ros2::ToGnssStatusMessage(state);

  EXPECT_EQ(msg.stamp.sec, 0);
  EXPECT_EQ(msg.stamp.nanosec, 0u);
  EXPECT_FALSE(msg.fix_valid);
  EXPECT_EQ(msg.fix_type, Msg::FIX_TYPE_UNKNOWN);
  EXPECT_EQ(msg.rtk_mode, Msg::RTK_MODE_UNKNOWN);
  EXPECT_EQ(msg.capability_flags, 0u);
  EXPECT_EQ(msg.value_flags, 0u);
  EXPECT_TRUE(std::isnan(msg.latitude_deg));
  EXPECT_TRUE(std::isnan(msg.longitude_deg));
  EXPECT_TRUE(std::isnan(msg.altitude_m));
  EXPECT_TRUE(std::isnan(msg.heading_deg));
  EXPECT_EQ(msg.satellites_used, 0u);
  EXPECT_FALSE(msg.jamming_detected);
  EXPECT_TRUE(universal_gnss_ros2::HasValidCapabilityValueInvariant(msg));
}

}  // namespace
