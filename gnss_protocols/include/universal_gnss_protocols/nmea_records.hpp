#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "universal_gnss_protocols/parser_base.hpp"

namespace universal_gnss_protocols
{

struct NmeaUtcTime
{
  std::uint8_t hour{0};
  std::uint8_t minute{0};
  double second{0.0};
};

struct NmeaDate
{
  std::uint8_t day{0};
  std::uint8_t month{0};
  std::uint8_t year_two_digits{0};
};

enum class NmeaGsaMode : std::uint8_t
{
  kUnknown = 0,
  kManual = 1,
  kAutomatic = 2,
};

enum class NmeaFixDimension : std::uint8_t
{
  kUnknown = 0,
  kNoFix = 1,
  k2D = 2,
  k3D = 3,
};

enum class NmeaGgaFixQuality : std::uint8_t
{
  kInvalid = 0,
  kGpsFix = 1,
  kDifferentialFix = 2,
  kPpsFix = 3,
  kRtkFixed = 4,
  kRtkFloat = 5,
  kEstimated = 6,
  kManual = 7,
  kSimulation = 8,
};

struct NmeaGgaRecord
{
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::optional<NmeaUtcTime> utc_time{};
  std::optional<double> latitude_deg{};
  std::optional<double> longitude_deg{};
  std::optional<double> altitude_m{};
  NmeaGgaFixQuality fix_quality{NmeaGgaFixQuality::kInvalid};
  bool fix_valid{false};
  std::optional<std::uint16_t> satellites_used{};
  std::optional<float> hdop{};
};

struct NmeaRmcRecord
{
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::optional<NmeaUtcTime> utc_time{};
  std::optional<NmeaDate> date{};
  std::optional<double> latitude_deg{};
  std::optional<double> longitude_deg{};
  std::optional<float> speed_over_ground_knots{};
  std::optional<float> course_over_ground_deg{};
  bool fix_valid{false};
};

struct NmeaGsaRecord
{
  static constexpr std::size_t kMaxActiveSatellites = 12;

  std::optional<ProtocolTimestampNs> timestamp_ns{};
  NmeaGsaMode fix_mode{NmeaGsaMode::kUnknown};
  NmeaFixDimension fix_dimension{NmeaFixDimension::kUnknown};
  std::optional<float> pdop{};
  std::optional<float> hdop{};
  std::optional<float> vdop{};
  std::array<std::optional<std::uint16_t>, kMaxActiveSatellites> active_satellite_prns{};
  std::size_t active_satellite_count{0};
};

struct NmeaGsvSatellite
{
  std::optional<std::uint16_t> prn{};
  std::optional<std::uint8_t> elevation_deg{};
  std::optional<std::uint16_t> azimuth_deg{};
  std::optional<float> cn0_db_hz{};
};

struct NmeaGsvRecord
{
  static constexpr std::size_t kMaxSatellitesPerSentence = 4;

  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::uint8_t total_messages{0};
  std::uint8_t message_index{0};
  std::uint16_t satellites_in_view{0};
  std::array<NmeaGsvSatellite, kMaxSatellitesPerSentence> satellites{};
  std::size_t satellite_count{0};
};

struct NmeaGstRecord
{
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::optional<NmeaUtcTime> utc_time{};
  std::optional<float> rms_range_residual_m{};
  std::optional<float> semi_major_std_dev_m{};
  std::optional<float> semi_minor_std_dev_m{};
  std::optional<float> orientation_deg{};
  std::optional<float> latitude_std_dev_m{};
  std::optional<float> longitude_std_dev_m{};
  std::optional<float> altitude_std_dev_m{};
};

struct NmeaVtgRecord
{
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::optional<float> true_course_deg{};
  std::optional<float> magnetic_course_deg{};
  std::optional<float> speed_knots{};
  std::optional<float> speed_kmh{};
  std::optional<char> mode_indicator{};
};

struct NmeaZdaRecord
{
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::optional<NmeaUtcTime> utc_time{};
  std::uint8_t day{0};
  std::uint8_t month{0};
  std::uint16_t year{0};
  std::optional<std::int8_t> local_zone_hours{};
  std::optional<std::int8_t> local_zone_minutes{};
};

}  // namespace universal_gnss_protocols
