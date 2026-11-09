#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/unicore_records.hpp"

namespace universal_gnss_protocols
{

inline constexpr std::uint8_t kUnicoreBinarySync1 = 0xAAu;
inline constexpr std::uint8_t kUnicoreBinarySync2 = 0x44u;
inline constexpr std::uint8_t kUnicoreBinarySync3 = 0xB5u;
inline constexpr std::size_t kUnicoreBinaryHeaderSize = 24u;
inline constexpr std::size_t kUnicoreBinaryCrcSize = 4u;

struct UnicoreBinaryFrame
{
  ProtocolType protocol{ProtocolType::kUnicore};
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::uint8_t header_length{static_cast<std::uint8_t>(kUnicoreBinaryHeaderSize)};
  std::uint8_t cpu_idle{0};
  std::uint16_t message_id{0};
  std::uint16_t payload_length{0};
  std::uint8_t time_ref{0};
  std::uint8_t time_status{0};
  std::uint16_t week_number{0};
  std::uint32_t milliseconds_of_week{0};
  std::uint32_t header_version{0};
  std::uint8_t reserved{0};
  std::uint8_t leap_seconds{0};
  std::uint16_t delay_ms{0};
  std::size_t payload_offset{kUnicoreBinaryHeaderSize};
  ByteVector payload{};
  ByteVector raw_bytes{};
  ChecksumStatus checksum_status{ChecksumStatus::kMissing};
  std::uint32_t reported_crc32{0};
  std::uint32_t computed_crc32{0};
};

struct UnicoreBinaryHeader
{
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::uint8_t cpu_idle_percent{0};
  std::uint16_t message_id{0};
  std::uint16_t payload_length{0};
  std::uint8_t time_reference_raw{0};
  std::uint8_t time_status_raw{0};
  std::uint16_t gps_week{0};
  std::uint32_t gps_millis_of_week{0};
  std::uint32_t format_version{0};
  std::uint8_t reserved{0};
  std::uint8_t leap_seconds{0};
  std::uint16_t output_delay_ms{0};
};

struct UnicoreBestNavBRecord
{
  UnicoreBinaryHeader header{};

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

struct UnicorePvtslnBRecord
{
  UnicoreBinaryHeader header{};

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

  std::optional<float> undulation_m{};
  std::optional<std::uint16_t> best_tracked_satellites{};
  std::optional<std::uint16_t> best_used_satellites{};
  std::optional<std::uint16_t> psr_tracked_satellites{};
  std::optional<std::uint16_t> psr_used_satellites{};

  UnicoreSolutionStatus baseline_solution_status{UnicoreSolutionStatus::kUnknown};
  std::optional<float> baseline_length_m{};
  std::optional<float> baseline_azimuth_deg{};
  std::optional<float> baseline_pitch_deg{};
  std::optional<std::uint16_t> baseline_tracked_satellites{};
  std::optional<std::uint16_t> baseline_used_satellites{};

  std::optional<float> gdop{};
  std::optional<float> pdop{};
  std::optional<float> hdop{};
  std::optional<float> htdop{};
  std::optional<float> tdop{};
};

}  // namespace universal_gnss_protocols
