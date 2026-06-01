#pragma once

#include <array>
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

enum class UnicoreJammingState : std::uint8_t
{
  kUnknown = 0,
  kNone = 1,
  kJamming = 2,
  kStrongJamming = 3,
};

enum class UnicoreSatelliteConstellation : std::uint8_t
{
  kUnknown = 0,
  kGps = 1,
  kGlonass = 2,
  kGalileo = 3,
  kBeiDou = 4,
  kQzss = 5,
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

constexpr std::size_t kMaxUnicoreBestSatEntries = 96u;

struct UnicoreBestSatSatellite
{
  UnicoreSatelliteConstellation constellation{UnicoreSatelliteConstellation::kUnknown};
  std::uint16_t satellite_id{0};
  std::optional<std::int16_t> glonass_frequency_channel{};
  bool status_good{false};
  std::uint32_t signal_mask{0};
  bool used_in_solution{false};
  bool common_view{false};
};

struct UnicoreBestSatRecord
{
  UnicoreAsciiHeader header{};

  std::uint16_t entry_count{0};
  std::uint16_t parsed_satellite_count{0};
  std::array<UnicoreBestSatSatellite, kMaxUnicoreBestSatEntries> satellites{};
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

constexpr std::size_t kMaxUnicoreSatsInfoSatellites = 64u;

struct UnicoreSatsInfoSatellite
{
  std::uint16_t satellite_id{0};
  std::int16_t azimuth_deg{0};
  std::int16_t elevation_deg{0};
  std::uint8_t system_id{0};
  std::uint8_t frequency_status{0};
  std::uint8_t frequency_count{0};
  std::uint8_t cn0_db_hz{0};
};

struct UnicoreSatsInfoRecord
{
  UnicoreAsciiHeader header{};

  std::uint16_t tracked_satellite_count{0};
  std::uint8_t version{0};
  std::uint8_t frequency_flag{0};
  std::uint16_t parsed_satellite_count{0};
  std::array<UnicoreSatsInfoSatellite, kMaxUnicoreSatsInfoSatellites> satellites{};
};

struct UnicoreJamStatusRecord
{
  UnicoreAsciiHeader header{};

  UnicorePositionType position_type{UnicorePositionType::kUnknown};
  std::uint8_t cw_ratio{0};
  UnicoreJammingState cw_state{UnicoreJammingState::kUnknown};
};

struct UnicoreFreqJamBandStatus
{
  std::uint8_t cw_ratio{0};
  UnicoreJammingState cw_state{UnicoreJammingState::kUnknown};
};

struct UnicoreFreqJamStatusRecord
{
  UnicoreAsciiHeader header{};

  UnicorePositionType position_type{UnicorePositionType::kUnknown};
  UnicoreFreqJamBandStatus l1{};
  UnicoreFreqJamBandStatus l2{};
  UnicoreFreqJamBandStatus l5{};
};

struct UnicoreHwStatusRecord
{
  UnicoreAsciiHeader header{};

  std::int32_t reserved_counter{0};
  float dc09_v{0.0f};
  float dc10_v{0.0f};
  float dc18_v{0.0f};
  bool clock_drift_valid{false};
  float clock_drift_mps{0.0f};
  std::uint8_t hw_flag{0};
  std::uint16_t pll_lock{0};
};

struct UnicoreAgcRecord
{
  UnicoreAsciiHeader header{};

  std::optional<std::int16_t> ant1_l1{};
  std::optional<std::int16_t> ant1_l2{};
  std::optional<std::int16_t> ant1_l5{};
  std::optional<std::int16_t> ant2_l1{};
  std::optional<std::int16_t> ant2_l2{};
  std::optional<std::int16_t> ant2_l5{};
};

}  // namespace universal_gnss_protocols
