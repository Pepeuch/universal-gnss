#include "universal_gnss_protocols/rtcm_parser.hpp"

namespace universal_gnss_protocols
{

namespace
{

bool IsRtcmMsmVariant(const std::uint16_t message_type)
{
  const std::uint16_t variant = static_cast<std::uint16_t>(message_type % 10u);
  return variant >= 1u && variant <= 7u;
}

}  // namespace

std::optional<std::uint16_t> ExtractRtcmMessageType(const ByteVector& payload)
{
  if (payload.size() < 2u)
  {
    return std::nullopt;
  }

  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(payload[0]) << 4u) |
                                    (static_cast<std::uint16_t>(payload[1]) >> 4u));
}

std::optional<std::uint16_t> ExtractRtcmMessageType(const RtcmFrame& frame)
{
  if (frame.protocol != ProtocolType::kRtcm3 ||
      frame.checksum_status != ChecksumStatus::kValid)
  {
    return std::nullopt;
  }

  return ExtractRtcmMessageType(frame.payload);
}

bool IsRtcmStationArpMessage(const std::uint16_t message_type)
{
  return message_type == 1005u || message_type == 1006u;
}

bool IsRtcmGlonassBiasMessage(const std::uint16_t message_type)
{
  return message_type == 1230u;
}

RtcmConstellation GetRtcmMsmConstellation(const std::uint16_t message_type)
{
  if (!IsRtcmMsmVariant(message_type))
  {
    return RtcmConstellation::kUnknown;
  }

  switch (message_type / 10u)
  {
    case 107u:
      return RtcmConstellation::kGps;
    case 108u:
      return RtcmConstellation::kGlonass;
    case 109u:
      return RtcmConstellation::kGalileo;
    case 110u:
      return RtcmConstellation::kSbas;
    case 111u:
      return RtcmConstellation::kQzss;
    case 112u:
      return RtcmConstellation::kBeiDou;
    case 113u:
      return RtcmConstellation::kNavIc;
    default:
      return RtcmConstellation::kUnknown;
  }
}

bool IsRtcmMsmMessage(const std::uint16_t message_type)
{
  return GetRtcmMsmConstellation(message_type) != RtcmConstellation::kUnknown;
}

ParserResult<RtcmMessageInfo> ParseRtcmMessageInfo(const RtcmFrame& frame)
{
  const std::optional<std::uint16_t> message_type = ExtractRtcmMessageType(frame);
  if (!message_type.has_value())
  {
    return ParserResult<RtcmMessageInfo>::InvalidData();
  }

  RtcmMessageInfo info;
  info.message_type = *message_type;
  info.is_station_arp = IsRtcmStationArpMessage(info.message_type);
  info.is_glonass_bias = IsRtcmGlonassBiasMessage(info.message_type);
  info.msm_constellation = GetRtcmMsmConstellation(info.message_type);
  info.is_msm = info.msm_constellation != RtcmConstellation::kUnknown;
  return ParserResult<RtcmMessageInfo>::RecordReady(info);
}

}  // namespace universal_gnss_protocols
