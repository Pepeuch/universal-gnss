#include <cmath>

#include <gtest/gtest.h>

#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_ros2/navsat_fix_adapter.hpp"

namespace
{

using NavSatFix = sensor_msgs::msg::NavSatFix;
using NavSatStatus = sensor_msgs::msg::NavSatStatus;

TEST(NavSatFixAdapterTest, DefaultUnknownStateMapsSafely)
{
  const universal_gnss::GnssRuntimeState state;
  const auto msg = universal_gnss_ros2::ToNavSatFixMessage(state);

  EXPECT_EQ(msg.header.stamp.sec, 0);
  EXPECT_EQ(msg.header.stamp.nanosec, 0u);
  EXPECT_TRUE(msg.header.frame_id.empty());
  EXPECT_EQ(msg.status.status, NavSatStatus::STATUS_NO_FIX);
  EXPECT_EQ(msg.status.service, 0u);
  EXPECT_TRUE(std::isnan(msg.latitude));
  EXPECT_TRUE(std::isnan(msg.longitude));
  EXPECT_TRUE(std::isnan(msg.altitude));
  EXPECT_EQ(msg.position_covariance_type, NavSatFix::COVARIANCE_TYPE_UNKNOWN);
}

TEST(NavSatFixAdapterTest, ExplicitNoFixKeepsNoFixStatusEvenWithCoordinates)
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = false;
  state.fix_type = universal_gnss::GnssFixType::kNoFix;
  state.latitude_deg = 48.123456;
  state.longitude_deg = 2.345678;
  state.altitude_m = 12.3;

  const auto msg = universal_gnss_ros2::ToNavSatFixMessage(state);

  EXPECT_EQ(msg.status.status, NavSatStatus::STATUS_NO_FIX);
  EXPECT_DOUBLE_EQ(msg.latitude, 48.123456);
  EXPECT_DOUBLE_EQ(msg.longitude, 2.345678);
  EXPECT_DOUBLE_EQ(msg.altitude, 12.3);
}

TEST(NavSatFixAdapterTest, MapsValidCoordinatesAndTimestamp)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = 1234567890LL;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = 48.123456;
  state.longitude_deg = 2.345678;
  state.altitude_m = 123.4;

  const auto msg = universal_gnss_ros2::ToNavSatFixMessage(state);

  EXPECT_EQ(msg.header.stamp.sec, 1);
  EXPECT_EQ(msg.header.stamp.nanosec, 234567890u);
  EXPECT_DOUBLE_EQ(msg.latitude, 48.123456);
  EXPECT_DOUBLE_EQ(msg.longitude, 2.345678);
  EXPECT_DOUBLE_EQ(msg.altitude, 123.4);
  EXPECT_EQ(msg.status.status, NavSatStatus::STATUS_FIX);
}

TEST(NavSatFixAdapterTest, KeepsCovarianceUnknownWhenPrecisionIsUnavailable)
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = 48.0;
  state.longitude_deg = 2.0;

  const auto msg = universal_gnss_ros2::ToNavSatFixMessage(state);

  EXPECT_EQ(msg.position_covariance_type, NavSatFix::COVARIANCE_TYPE_UNKNOWN);
  EXPECT_DOUBLE_EQ(msg.position_covariance[0], 0.0);
  EXPECT_DOUBLE_EQ(msg.position_covariance[4], 0.0);
  EXPECT_DOUBLE_EQ(msg.position_covariance[8], 0.0);
}

TEST(NavSatFixAdapterTest, DerivesConservativeApproximateCovarianceWhenAvailable)
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = 48.0;
  state.longitude_deg = 2.0;
  state.altitude_m = 100.0;
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHorizontalAccuracy);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kVerticalAccuracy);
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kHorizontalAccuracy, state.horizontal_accuracy_m,
      0.2f));
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kVerticalAccuracy, state.vertical_accuracy_m,
      0.5f));

  const auto msg = universal_gnss_ros2::ToNavSatFixMessage(state);

  EXPECT_EQ(msg.position_covariance_type, NavSatFix::COVARIANCE_TYPE_APPROXIMATED);
  EXPECT_NEAR(msg.position_covariance[0], 0.04, 1e-9);
  EXPECT_NEAR(msg.position_covariance[4], 0.04, 1e-9);
  EXPECT_NEAR(msg.position_covariance[8], 0.25, 1e-9);
}

TEST(NavSatFixAdapterTest, DoesNotInventPartialCovarianceFromSingleAxisAccuracy)
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = 48.0;
  state.longitude_deg = 2.0;
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHorizontalAccuracy);
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kHorizontalAccuracy, state.horizontal_accuracy_m,
      0.2f));

  const auto msg = universal_gnss_ros2::ToNavSatFixMessage(state);

  EXPECT_EQ(msg.position_covariance_type, NavSatFix::COVARIANCE_TYPE_UNKNOWN);
  EXPECT_DOUBLE_EQ(msg.position_covariance[0], 0.0);
  EXPECT_DOUBLE_EQ(msg.position_covariance[4], 0.0);
  EXPECT_DOUBLE_EQ(msg.position_covariance[8], 0.0);
}

TEST(NavSatFixAdapterTest, MapsOnlyExplicitRtkFixedToGbasStatus)
{
  universal_gnss::GnssRuntimeState fixed_state;
  fixed_state.fix_valid = true;
  fixed_state.fix_type = universal_gnss::GnssFixType::kRtkFixed;
  fixed_state.latitude_deg = 48.0;
  fixed_state.longitude_deg = 2.0;

  const auto fixed_msg = universal_gnss_ros2::ToNavSatFixMessage(fixed_state);
  EXPECT_EQ(fixed_msg.status.status, NavSatStatus::STATUS_GBAS_FIX);

  universal_gnss::GnssRuntimeState float_state;
  float_state.fix_valid = true;
  float_state.fix_type = universal_gnss::GnssFixType::kRtkFloat;
  float_state.latitude_deg = 48.0;
  float_state.longitude_deg = 2.0;
  universal_gnss::SetCapability(float_state, universal_gnss::GnssCapability::kRtkMode);
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      float_state, universal_gnss::GnssCapability::kRtkMode, float_state.rtk_mode,
      universal_gnss::GnssRtkMode::kFloat));

  const auto float_msg = universal_gnss_ros2::ToNavSatFixMessage(float_state);
  EXPECT_EQ(float_msg.status.status, NavSatStatus::STATUS_FIX);
}

TEST(NavSatFixAdapterTest, DoesNotTrustRtkModeWithoutRuntimeValueFlag)
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = 48.0;
  state.longitude_deg = 2.0;
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kRtkMode);
  state.rtk_mode = universal_gnss::GnssRtkMode::kFixed;

  const auto msg = universal_gnss_ros2::ToNavSatFixMessage(state);

  EXPECT_EQ(msg.status.status, NavSatStatus::STATUS_FIX);
}

TEST(NavSatFixAdapterTest, DoesNotTrustAccuracyFieldsWithoutRuntimeValueFlags)
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = 48.0;
  state.longitude_deg = 2.0;
  state.altitude_m = 100.0;
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHorizontalAccuracy);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kVerticalAccuracy);
  state.horizontal_accuracy_m = 0.2f;
  state.vertical_accuracy_m = 0.5f;

  const auto msg = universal_gnss_ros2::ToNavSatFixMessage(state);

  EXPECT_EQ(msg.position_covariance_type, NavSatFix::COVARIANCE_TYPE_UNKNOWN);
  EXPECT_DOUBLE_EQ(msg.position_covariance[0], 0.0);
  EXPECT_DOUBLE_EQ(msg.position_covariance[4], 0.0);
  EXPECT_DOUBLE_EQ(msg.position_covariance[8], 0.0);
}

TEST(NavSatFixAdapterTest, KeepsNormalizedGenericFixAsStatusFixEvenWithCorrectionAge)
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = 48.0;
  state.longitude_deg = 2.0;
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kCorrectionAge);
  EXPECT_TRUE(universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kCorrectionAge, state.correction_age_s, 0.7f));

  const auto msg = universal_gnss_ros2::ToNavSatFixMessage(state);

  EXPECT_EQ(msg.status.status, NavSatStatus::STATUS_FIX);
  EXPECT_EQ(msg.status.service, 0u);
}

TEST(NavSatFixAdapterTest, MissingOptionalFieldsRemainSafe)
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = 48.0;
  state.longitude_deg = 2.0;

  const auto msg = universal_gnss_ros2::ToNavSatFixMessage(state);

  EXPECT_TRUE(std::isnan(msg.altitude));
  EXPECT_EQ(msg.status.service, 0u);
  EXPECT_EQ(msg.position_covariance_type, NavSatFix::COVARIANCE_TYPE_UNKNOWN);
}

}  // namespace
