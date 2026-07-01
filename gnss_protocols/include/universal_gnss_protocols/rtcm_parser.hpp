#pragma once

#include <cstdint>
#include <optional>

#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/rtcm_records.hpp"

namespace universal_gnss_protocols
{

std::optional<std::uint16_t> ExtractRtcmMessageType(const ByteVector& payload);

std::optional<std::uint16_t> ExtractRtcmMessageType(const RtcmFrame& frame);

bool IsRtcmStationArpMessage(std::uint16_t message_type);

bool IsRtcmGlonassBiasMessage(std::uint16_t message_type);

bool IsRtcmMsmMessage(std::uint16_t message_type);

RtcmConstellation GetRtcmMsmConstellation(std::uint16_t message_type);

ParserResult<RtcmMessageInfo> ParseRtcmMessageInfo(const RtcmFrame& frame);

ParserResult<RtcmBaseStationArpRecord> ParseRtcmBaseStationArp(const RtcmFrame& frame);

ParserResult<RtcmGlonassCodePhaseBiasRecord> ParseRtcmGlonassCodePhaseBias(
    const RtcmFrame& frame);

}  // namespace universal_gnss_protocols
