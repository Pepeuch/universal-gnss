#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_protocols/parser_base.hpp"
#include "universal_gnss_protocols/protocol_type.hpp"

namespace universal_gnss_protocols
{

enum class ChecksumStatus : std::uint8_t
{
  kNotChecked = 0,
  kMissing = 1,
  kValid = 2,
  kInvalid = 3,
};

using ByteVector = std::vector<std::uint8_t>;

struct NmeaSentence
{
  ProtocolType protocol{ProtocolType::kNmea};
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  char leader{'$'};
  std::string talker{};
  std::string sentence_type{};
  std::string payload_text{};
  ByteVector raw_bytes{};
  ChecksumStatus checksum_status{ChecksumStatus::kMissing};
  std::optional<std::uint8_t> reported_checksum{};
  std::optional<std::uint8_t> computed_checksum{};
};

struct RtcmFrame
{
  ProtocolType protocol{ProtocolType::kRtcm3};
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::uint16_t message_type{0};
  ByteVector payload{};
  ByteVector raw_bytes{};
  ChecksumStatus checksum_status{ChecksumStatus::kMissing};
  std::uint32_t reported_crc24q{0};
  std::uint32_t computed_crc24q{0};
};

struct UbxFrame
{
  ProtocolType protocol{ProtocolType::kUbx};
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  std::uint8_t class_id{0};
  std::uint8_t message_id{0};
  ByteVector payload{};
  ByteVector raw_bytes{};
  ChecksumStatus checksum_status{ChecksumStatus::kMissing};
  std::optional<std::uint8_t> reported_ck_a{};
  std::optional<std::uint8_t> reported_ck_b{};
  std::uint8_t computed_ck_a{0};
  std::uint8_t computed_ck_b{0};
};

struct UnicoreFrame
{
  ProtocolType protocol{ProtocolType::kUnicore};
  std::optional<ProtocolTimestampNs> timestamp_ns{};
  char sync_char{'#'};
  std::string message_name{};
  ByteVector payload{};
  ByteVector raw_bytes{};
  ChecksumStatus checksum_status{ChecksumStatus::kNotChecked};
  std::uint32_t reported_crc32{0};
  std::uint32_t computed_crc32{0};
};

}  // namespace universal_gnss_protocols
