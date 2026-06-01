#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "universal_gnss_protocols/protocol_records.hpp"

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

}  // namespace universal_gnss_protocols
