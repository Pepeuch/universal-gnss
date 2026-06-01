#include "universal_gnss_ros2/navsat_fix_adapter.hpp"

#include <cmath>
#include <limits>

#include "universal_gnss_ros2/gnss_status_adapter.hpp"

namespace universal_gnss_ros2
{

namespace
{

double QuietNaN()
{
  return std::numeric_limits<double>::quiet_NaN();
}

bool HasUsableCoordinates(const universal_gnss::GnssRuntimeState& state)
{
  return state.latitude_deg.has_value() && state.longitude_deg.has_value();
}

bool HasExplicitRtkFixed(const universal_gnss::GnssRuntimeState& state)
{
  if (state.fix_type == universal_gnss::GnssFixType::kRtkFixed)
  {
    return true;
  }

  return universal_gnss::HasValueAvailable(state, universal_gnss::GnssCapability::kRtkMode) &&
         state.rtk_mode.has_value() &&
         *state.rtk_mode == universal_gnss::GnssRtkMode::kFixed;
}

void PopulateConservativeCovariance(const universal_gnss::GnssRuntimeState& state,
                                    sensor_msgs::msg::NavSatFix& message)
{
  using NavSatFix = sensor_msgs::msg::NavSatFix;

  // The core model only exposes scalar horizontal/vertical accuracy, not a
  // full covariance matrix or axis-specific ellipsoid. We therefore publish a
  // conservative diagonal approximation only when both standard-deviation-like
  // terms are present. If either term is missing, we leave covariance unknown
  // rather than inventing partial precision.
  if (!universal_gnss::HasValueAvailable(state, universal_gnss::GnssCapability::kHorizontalAccuracy) ||
      !universal_gnss::HasValueAvailable(state, universal_gnss::GnssCapability::kVerticalAccuracy) ||
      !state.horizontal_accuracy_m.has_value() || !state.vertical_accuracy_m.has_value())
  {
    message.position_covariance_type = NavSatFix::COVARIANCE_TYPE_UNKNOWN;
    return;
  }

  const double horizontal_sigma = static_cast<double>(*state.horizontal_accuracy_m);
  const double vertical_sigma = static_cast<double>(*state.vertical_accuracy_m);
  const double horizontal_variance = horizontal_sigma * horizontal_sigma;
  const double vertical_variance = vertical_sigma * vertical_sigma;

  message.position_covariance[0] = horizontal_variance;
  message.position_covariance[4] = horizontal_variance;
  message.position_covariance[8] = vertical_variance;
  message.position_covariance_type = NavSatFix::COVARIANCE_TYPE_APPROXIMATED;
}

}  // namespace

sensor_msgs::msg::NavSatFix ToNavSatFixMessage(const universal_gnss::GnssRuntimeState& state)
{
  using NavSatFix = sensor_msgs::msg::NavSatFix;
  using NavSatStatus = sensor_msgs::msg::NavSatStatus;

  NavSatFix message;
  message.header.stamp = ToRosTime(state.timestamp_ns);

  message.latitude = QuietNaN();
  message.longitude = QuietNaN();
  message.altitude = QuietNaN();
  message.status.status = NavSatStatus::STATUS_NO_FIX;
  message.status.service = 0u;
  message.position_covariance_type = NavSatFix::COVARIANCE_TYPE_UNKNOWN;

  if (!HasUsableCoordinates(state))
  {
    return message;
  }

  message.latitude = *state.latitude_deg;
  message.longitude = *state.longitude_deg;
  message.altitude = state.altitude_m.has_value() ? *state.altitude_m : QuietNaN();

  if (!state.fix_valid ||
      state.fix_type == universal_gnss::GnssFixType::kUnknown ||
      state.fix_type == universal_gnss::GnssFixType::kNoFix)
  {
    return message;
  }

  message.status.status = HasExplicitRtkFixed(state) ? NavSatStatus::STATUS_GBAS_FIX
                                                     : NavSatStatus::STATUS_FIX;
  PopulateConservativeCovariance(state, message);
  return message;
}

}  // namespace universal_gnss_ros2
