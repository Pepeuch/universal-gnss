// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_base_operation.hpp"
#include "universal_gnss_driver/receiver_discovery.hpp"
#include <array>
namespace ug::embedded
{
universal_gnss_driver::ReceiverDetectedFamily
SelectReadOnlyQueryFamily(universal_gnss_driver::ReceiverDetectedFamily detected,
                          std::string_view selector);
struct TelemetryOperation
{
  std::uint8_t rate_hz{1}, receiver_port{1};
  bool persist{}, confirm_persistent{}, set_constellations{};
  std::array<bool, 4> constellations{};         // GPS, Galileo, BeiDou, GLONASS (u-blox only).
  std::array<std::uint8_t, 2> signal_groups{};  // Existing UG model tables (Unicore only).
  std::uint8_t signal_group_count{};
};
bool DecodeTelemetryOperation(std::string_view, TelemetryOperation&, std::string& error);
universal_gnss_driver::BaseConfigPlan
BuildTelemetryPlan(const TelemetryOperation&, std::string_view verified_model, bool model_verified);
bool DecodeDetectionOperation(std::string_view, std::string& error);
bool DecodeReceiverRestart(std::string_view, std::string& error);
universal_gnss_driver::BaseConfigPlan BuildReceiverRestart(std::string_view model, bool verified);
struct ReceiverPlanCapabilities
{
  bool telemetry{}, fixed{}, survey_in{}, time_optimization{}, disable_base{}, restart{};
};
ReceiverPlanCapabilities InspectReceiverPlans(std::string_view verified_model, bool verified);
struct GnssOperation
{
  enum class Kind : std::uint8_t
  {
    kBase,
    kTelemetry,
    kDetect,
    kBaseDisable,
    kRestart
  };
  Kind kind{Kind::kBase};
  BaseOperation base{};
  TelemetryOperation telemetry{};
  BaseDisableOperation disable_base{};
};
}  // namespace ug::embedded
