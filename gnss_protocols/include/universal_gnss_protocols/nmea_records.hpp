#pragma once

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

}  // namespace universal_gnss_protocols
