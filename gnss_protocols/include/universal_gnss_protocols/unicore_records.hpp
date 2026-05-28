#pragma once

#include <cstdint>
#include <optional>

#include "universal_gnss_protocols/parser_base.hpp"

namespace universal_gnss_protocols
{

enum class UnicoreTimeReference : std::uint8_t
{
  kUnknown = 0,
  kGps = 1,
  kBds = 2,
};

enum class UnicoreTimeStatus : std::uint8_t
{
  kUnknown = 0,
  kFine = 1,
};

enum class UnicorePositionType : std::uint8_t
{
  kUnknown = 0,
  kNone = 1,
  kFixedPos = 2,
  kFixedHeight = 3,
  kDopplerVelocity = 4,
  kSingle = 5,
  kPsrDiff = 6,
  kSbas = 7,
  kL1Float = 8,
  kIonoFreeFloat = 9,
  kNarrowFloat = 10,
  kL1Int = 11,
  kWideInt = 12,
  kNarrowInt = 13,
  kIns = 14,
  kInsPsrsp = 15,
  kInsPsrDiff = 16,
  kInsRtkFloat = 17,
  kInsRtkFixed = 18,
  kPppConverging = 19,
  kPpp = 20,
};

enum class UnicoreSolutionStatus : std::uint8_t
{
  kUnknown = 0,
  kSolComputed = 1,
  kInsufficientObs = 2,
  kNoConvergence = 3,
  kCovTrace = 4,
};

enum class UnicoreDualAntennaStatus : std::uint8_t
{
  kUnknown = 0,
  kNotSolved = 1,
  kWithinLimit = 2,
  kOutOfLimit = 3,
  kNotConfigured = 4,
};

struct UnicoreAsciiHeader
{
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::uint8_t cpu_idle_percent{0};
  UnicoreTimeReference time_reference{UnicoreTimeReference::kUnknown};
  UnicoreTimeStatus time_status{UnicoreTimeStatus::kUnknown};
  std::uint16_t gps_week{0};
  std::uint32_t gps_millis_of_week{0};
  std::uint32_t format_version{0};
  std::uint8_t leap_seconds{0};
  std::uint16_t output_delay_ms{0};
};

struct UnicorePvtslnRecord
{
  UnicoreAsciiHeader header{};

  UnicorePositionType best_position_type{UnicorePositionType::kUnknown};
  double best_altitude_m{0.0};
  double best_latitude_deg{0.0};
  double best_longitude_deg{0.0};
  std::optional<float> best_altitude_std_m{};
  std::optional<float> best_latitude_std_m{};
  std::optional<float> best_longitude_std_m{};
  std::optional<float> best_diff_age_s{};

  UnicorePositionType psr_position_type{UnicorePositionType::kUnknown};
  std::optional<double> psr_altitude_m{};
  std::optional<double> psr_latitude_deg{};
  std::optional<double> psr_longitude_deg{};

  std::optional<std::uint16_t> best_tracked_satellites{};
  std::optional<std::uint16_t> best_used_satellites{};
  std::optional<std::uint16_t> psr_tracked_satellites{};
  std::optional<std::uint16_t> psr_used_satellites{};

  UnicoreSolutionStatus heading_status{UnicoreSolutionStatus::kUnknown};
  std::optional<float> heading_length_m{};
  std::optional<float> heading_deg{};
  std::optional<float> heading_pitch_deg{};
  std::optional<std::uint16_t> heading_tracked_satellites{};
  std::optional<std::uint16_t> heading_used_satellites{};

  std::optional<float> gdop{};
  std::optional<float> pdop{};
  std::optional<float> hdop{};
  std::optional<float> htdop{};
  std::optional<float> tdop{};
};

struct UnicoreBestNavRecord
{
  UnicoreAsciiHeader header{};

  UnicoreSolutionStatus solution_status{UnicoreSolutionStatus::kUnknown};
  UnicorePositionType position_type{UnicorePositionType::kUnknown};
  double latitude_deg{0.0};
  double longitude_deg{0.0};
  double altitude_m{0.0};
  std::optional<float> undulation_m{};
  std::optional<bool> datum_is_wgs84{};
  std::optional<float> latitude_std_m{};
  std::optional<float> longitude_std_m{};
  std::optional<float> altitude_std_m{};
  std::optional<float> diff_age_s{};
  std::optional<float> solution_age_s{};
  std::optional<std::uint16_t> tracked_satellites{};
  std::optional<std::uint16_t> used_satellites{};
};

struct UnicoreRtkStatusRecord
{
  UnicoreAsciiHeader header{};

  UnicorePositionType position_type{UnicorePositionType::kUnknown};
  std::optional<std::uint32_t> calculation_status{};
  std::optional<std::uint8_t> ionosphere_effect{};
  UnicoreDualAntennaStatus dual_antenna_status{UnicoreDualAntennaStatus::kUnknown};
  std::optional<std::uint16_t> adr_observation_count{};
};

struct UnicoreRtcmStatusRecord
{
  UnicoreAsciiHeader header{};

  std::uint32_t message_type{0};
  std::uint32_t message_count{0};
  std::uint32_t base_station_id{0};
  std::uint32_t satellites_in_message{0};
  std::uint8_t l1_observables{0};
  std::uint8_t l2_observables{0};
  std::uint8_t l3_observables{0};
  std::uint8_t l4_observables{0};
  std::uint8_t l5_observables{0};
  std::uint8_t l6_observables{0};
};

}  // namespace universal_gnss_protocols
