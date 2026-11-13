// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss_driver/receiver_command.hpp"

namespace universal_gnss_driver
{
enum class BaseMode : std::uint8_t
{
  kFixed,
  kSurveyIn,
  kUnicoreTimeOptimization
};
enum class BaseHeightDatum : std::uint8_t
{
  kEllipsoidal,
  kOrthometric
};
struct BaseConfigRequest
{
  ReceiverVendor vendor{ReceiverVendor::kUnknown};
  std::string_view model{};
  // Owner must establish identity from a verified vendor response, not telemetry/ACK.
  bool model_verified{false};
  BaseMode mode{BaseMode::kFixed};
  BaseHeightDatum height_datum{BaseHeightDatum::kEllipsoidal};
  double latitude_deg{0}, longitude_deg{0}, height_m{0};
  std::uint32_t duration_s{60};
  std::uint32_t accuracy_01mm{10000};  // F9P survey-in standard-deviation limit.
  double reuse_distance_m{0};          // Unicore flash-position reuse distance, NOT accuracy.
  std::uint8_t uart_port{1};
  bool rtcm_output{true};
  bool persist{false};
  bool confirm_persistent{false};
};
struct BaseConfigPlan
{
  std::vector<ReceiverCommand> commands{};
  std::string error{};
  bool ok() const
  {
    return error.empty() && !commands.empty();
  }
};
// Uses existing UG vendor models, command safety and UBX frame construction.
// Does not send commands, fabricate survey completion or mutate runtime state.
BaseConfigPlan BuildBaseConfigProfile(const BaseConfigRequest& request);
}  // namespace universal_gnss_driver
