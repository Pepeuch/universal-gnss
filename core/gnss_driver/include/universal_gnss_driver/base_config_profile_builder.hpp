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
struct BaseDisableRequest
{
  ReceiverVendor vendor{ReceiverVendor::kUnknown};
  std::string_view model{};
  bool model_verified{false};
  std::uint8_t uart_port{1};
  bool persist{false}, confirm_persistent{false};
};
// Verified F9P or UM980/UM982: leave BASE mode and disable the native profile's
// RTCM outputs on the selected UART. Receiver mode is global; no other port's
// logging rules are explicitly rewritten. F9P coordinate keys are not written.
// Unicore uses generic MODE ROVER without an explicit reset/erase command, not
// restoration of an earlier BASE/rover configuration. Persistence requires approval.
// ACK is command acceptance, not physical stop evidence. Unsupported models fail closed.
BaseConfigPlan BuildBaseDisableProfile(const BaseDisableRequest& request);
}  // namespace universal_gnss_driver
