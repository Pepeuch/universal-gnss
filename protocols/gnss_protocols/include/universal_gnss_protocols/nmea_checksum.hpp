#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "universal_gnss_protocols/protocol_records.hpp"

namespace universal_gnss_protocols
{

std::uint8_t ComputeNmeaChecksum(std::string_view payload_text);

bool TryParseHexByte(std::string_view text, std::uint8_t& value);

ChecksumStatus ValidateNmeaChecksum(std::string_view frame,
                                    std::optional<std::uint8_t>* reported_checksum = nullptr,
                                    std::optional<std::uint8_t>* computed_checksum = nullptr);

}  // namespace universal_gnss_protocols
