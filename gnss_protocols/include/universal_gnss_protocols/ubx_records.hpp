#pragma once

#include <array>
#include <cstddef>
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

using UbxNavStatusFixType = UbxNavPvtFixType;

enum class UbxAckMessageKind : std::uint8_t
{
  kNak = 0x00u,
  kAck = 0x01u,
};

struct UbxAckRecord
{
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  UbxAckMessageKind kind{UbxAckMessageKind::kNak};
  std::uint8_t target_class_id{0};
  std::uint8_t target_message_id{0};
};

struct UbxNavStatusRecord
{
  std::optional<ProtocolTimestampNs> timestamp_ns{};

  std::uint32_t i_tow_ms{0};
  UbxNavStatusFixType gps_fix{UbxNavStatusFixType::kNoFix};
  std::uint8_t flags{0};
  std::uint8_t fix_stat{0};
  std::uint8_t flags2{0};

  std::uint32_t ttff_ms{0};
  std::uint32_t msss_ms{0};

  bool gnss_fix_ok{false};
  bool differential_solution{false};
  bool carrier_solution_valid{false};
  UbxCarrierSolutionStatus carrier_solution{UbxCarrierSolutionStatus::kNone};
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

enum class UbxNavSatHealth : std::uint8_t
{
  kUnknown = 0,
  kHealthy = 1,
  kUnhealthy = 2,
};

struct UbxNavSatSatellite
{
  std::uint8_t gnss_id{0};
  std::uint8_t sv_id{0};
  std::uint8_t cno_db_hz{0};
  std::int8_t elevation_deg{0};
  std::int16_t azimuth_deg{0};
  std::uint8_t quality_indicator{0};
  bool used_in_navigation{false};
  std::optional<bool> healthy{};
};

struct UbxNavSatRecord
{
  static constexpr std::size_t kMaxSatellites = 64;

  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::uint32_t i_tow_ms{0};
  std::uint8_t version{0};
  std::uint8_t num_svs{0};
  std::array<UbxNavSatSatellite, kMaxSatellites> satellites{};
  std::size_t satellite_count{0};
  std::uint8_t used_satellite_count{0};
};

enum class UbxMonRfJammingState : std::uint8_t
{
  kUnknown = 0,
  kOk = 1,
  kWarning = 2,
  kCritical = 3,
};

struct UbxMonRfBlock
{
  std::uint8_t block_id{0};
  std::uint8_t flags{0};
  UbxMonRfJammingState jamming_state{UbxMonRfJammingState::kUnknown};
  std::uint8_t antenna_status{0};
  std::uint8_t antenna_power{0};
  std::uint32_t post_status{0};
  std::uint16_t noise_per_ms{0};
  std::uint16_t agc_count{0};
  std::uint8_t cw_suppression{0};
};

struct UbxMonRfRecord
{
  static constexpr std::size_t kMaxBlocks = 8;

  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::uint8_t version{0};
  std::uint8_t block_count{0};
  std::array<UbxMonRfBlock, kMaxBlocks> blocks{};
};

}  // namespace universal_gnss_protocols
