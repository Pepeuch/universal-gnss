#pragma once

#include <cstdint>
#include <optional>

#include "universal_gnss_protocols/parser_base.hpp"

namespace universal_gnss_protocols
{

enum class UbxNavPvtFixType : std::uint8_t
{
  kNoFix = 0,
  kDeadReckoningOnly = 1,
  k2D = 2,
  k3D = 3,
  kGnssDeadReckoningCombined = 4,
  kTimeOnly = 5,
};

enum class UbxCarrierSolutionStatus : std::uint8_t
{
  kNone = 0,
  kFloat = 1,
  kFixed = 2,
};

struct UbxNavPvtRecord
{
  std::optional<ProtocolTimestampNs> timestamp_ns{};

  std::uint32_t i_tow_ms{0};
  std::uint16_t year{0};
  std::uint8_t month{0};
  std::uint8_t day{0};
  std::uint8_t hour{0};
  std::uint8_t minute{0};
  std::uint8_t second{0};
  std::int32_t nano_ns{0};

  bool valid_date{false};
  bool valid_time{false};
  bool fully_resolved_time{false};

  UbxNavPvtFixType fix_type{UbxNavPvtFixType::kNoFix};
  std::uint8_t flags{0};
  std::uint8_t flags2{0};
  std::uint16_t flags3{0};

  bool gnss_fix_ok{false};
  bool differential_solution{false};
  bool heading_vehicle_valid{false};
  bool invalid_llh{false};

  UbxCarrierSolutionStatus carrier_solution{UbxCarrierSolutionStatus::kNone};

  std::uint8_t num_sv{0};

  double longitude_deg{0.0};
  double latitude_deg{0.0};
  double height_ellipsoid_m{0.0};
  double height_msl_m{0.0};
  float horizontal_accuracy_m{0.0f};
  float vertical_accuracy_m{0.0f};

  std::int32_t vel_north_mm_s{0};
  std::int32_t vel_east_mm_s{0};
  std::int32_t vel_down_mm_s{0};
  std::int32_t ground_speed_mm_s{0};
  float heading_motion_deg{0.0f};
  float heading_vehicle_deg{0.0f};
  float heading_accuracy_deg{0.0f};
};

}  // namespace universal_gnss_protocols
