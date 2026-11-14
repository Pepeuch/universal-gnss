// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss_driver/base_config_profile_builder.hpp"
namespace ug::embedded
{
struct BaseOperation
{
  universal_gnss_driver::BaseMode mode{universal_gnss_driver::BaseMode::kFixed};
  universal_gnss_driver::BaseHeightDatum datum{
      universal_gnss_driver::BaseHeightDatum::kEllipsoidal};
  double latitude_deg{}, longitude_deg{}, height_m{}, reuse_distance_m{};
  std::uint8_t receiver_port{1};
  std::uint32_t duration_s{60}, accuracy_01mm{10000};
  bool rtcm_output{true}, persist{false}, confirm_persistent{false};
};
bool DecodeBaseOperation(std::string_view, BaseOperation&, std::string& error);
struct BaseDisableOperation
{
  std::uint8_t receiver_port{1};
  bool persist{}, confirm_persistent{};
};
bool DecodeBaseDisableOperation(std::string_view, BaseDisableOperation&, std::string& error);
}  // namespace ug::embedded
