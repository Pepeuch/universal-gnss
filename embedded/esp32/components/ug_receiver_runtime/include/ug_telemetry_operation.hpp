// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_base_operation.hpp"
#include <array>
namespace ug::embedded
{
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
struct GnssOperation
{
  enum class Kind : std::uint8_t
  {
    kBase,
    kTelemetry
  };
  Kind kind{Kind::kBase};
  BaseOperation base{};
  TelemetryOperation telemetry{};
};
}  // namespace ug::embedded
