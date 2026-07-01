#include "universal_gnss_protocols/rtcm_parser.hpp"

namespace universal_gnss_protocols
{

namespace
{

constexpr double kRtcmArpCoordinateScaleM = 0.0001;
constexpr double kRtcmGlonassCodePhaseBiasScaleM = 0.02;
constexpr std::size_t kRtcm1005Bits = 153u;
constexpr std::size_t kRtcm1006Bits = 169u;
constexpr std::size_t kRtcm1230HeaderBits = 32u;
constexpr std::size_t kRtcmMsmHeaderBits = 169u;

bool IsRtcmMsmVariant(const std::uint16_t message_type)
{
  const std::uint16_t variant = static_cast<std::uint16_t>(message_type % 10u);
  return variant >= 1u && variant <= 7u;
}

std::optional<std::uint64_t> ReadRtcmUnsignedBits(const ByteVector& payload,
                                                  const std::size_t bit_offset,
                                                  const std::size_t bit_count)
{
  if (bit_count == 0u || bit_count > 64u)
  {
    return std::nullopt;
  }

  const std::size_t total_bits = payload.size() * 8u;
  if (bit_offset + bit_count > total_bits)
  {
    return std::nullopt;
  }

  std::uint64_t value = 0u;
  for (std::size_t i = 0u; i < bit_count; ++i)
  {
    const std::size_t absolute_bit = bit_offset + i;
    const std::size_t byte_index = absolute_bit / 8u;
    const std::size_t bit_index = 7u - (absolute_bit % 8u);
    const std::uint8_t bit =
        static_cast<std::uint8_t>((payload[byte_index] >> bit_index) & 0x01u);
    value = (value << 1u) | static_cast<std::uint64_t>(bit);
  }

  return value;
}

std::optional<std::int64_t> ReadRtcmSignedBits(const ByteVector& payload,
                                               const std::size_t bit_offset,
                                               const std::size_t bit_count)
{
  if (bit_count == 0u || bit_count >= 64u)
  {
    return std::nullopt;
  }

  const auto raw_value = ReadRtcmUnsignedBits(payload, bit_offset, bit_count);
  if (!raw_value.has_value())
  {
    return std::nullopt;
  }

  std::uint64_t extended_value = *raw_value;
  const std::uint64_t sign_mask = 1ULL << (bit_count - 1u);
  if ((extended_value & sign_mask) != 0u)
  {
    extended_value |= (~0ULL << bit_count);
  }

  return static_cast<std::int64_t>(extended_value);
}

std::uint8_t CountSetBits(std::uint64_t value)
{
  std::uint8_t count = 0u;
  while (value != 0u)
  {
    count = static_cast<std::uint8_t>(count + static_cast<std::uint8_t>(value & 0x01u));
    value >>= 1u;
  }
  return count;
}

std::optional<std::uint16_t> CountSetBitsInRange(const ByteVector& payload,
                                                 const std::size_t bit_offset,
                                                 const std::size_t bit_count)
{
  const std::size_t total_bits = payload.size() * 8u;
  if (bit_offset + bit_count > total_bits)
  {
    return std::nullopt;
  }

  std::uint16_t count = 0u;
  for (std::size_t index = 0u; index < bit_count; ++index)
  {
    const auto bit = ReadRtcmUnsignedBits(payload, bit_offset + index, 1u);
    if (!bit.has_value())
    {
      return std::nullopt;
    }

    count = static_cast<std::uint16_t>(count + (*bit != 0u ? 1u : 0u));
  }

  return count;
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

std::uint8_t GetRtcmMsmVariant(const std::uint16_t message_type)
{
  if (!IsRtcmMsmVariant(message_type))
  {
    return 0u;
  }

  return static_cast<std::uint8_t>(message_type % 10u);
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
  info.msm_variant = GetRtcmMsmVariant(info.message_type);
  return ParserResult<RtcmMessageInfo>::RecordReady(info);
}

ParserResult<RtcmBaseStationArpRecord> ParseRtcmBaseStationArp(const RtcmFrame& frame)
{
  const std::optional<std::uint16_t> message_type = ExtractRtcmMessageType(frame);
  if (!message_type.has_value() || !IsRtcmStationArpMessage(*message_type))
  {
    return ParserResult<RtcmBaseStationArpRecord>::InvalidData();
  }

  const std::size_t required_bits = *message_type == 1006u ? kRtcm1006Bits : kRtcm1005Bits;
  if (frame.payload.size() * 8u < required_bits)
  {
    return ParserResult<RtcmBaseStationArpRecord>::InvalidData();
  }

  std::size_t bit_offset = 0u;
  const auto read_u = [&](const std::size_t bit_count) {
    const auto value = ReadRtcmUnsignedBits(frame.payload, bit_offset, bit_count);
    if (value.has_value())
    {
      bit_offset += bit_count;
    }
    return value;
  };
  const auto read_s = [&](const std::size_t bit_count) {
    const auto value = ReadRtcmSignedBits(frame.payload, bit_offset, bit_count);
    if (value.has_value())
    {
      bit_offset += bit_count;
    }
    return value;
  };

  const auto parsed_message_type = read_u(12u);
  const auto station_id = read_u(12u);
  const auto itrf_year = read_u(6u);
  const auto gps_indicator = read_u(1u);
  const auto glonass_indicator = read_u(1u);
  const auto reserved_a = read_u(1u);
  const auto galileo_indicator = read_u(1u);
  const auto reference_station_indicator = read_u(1u);
  const auto ecef_x = read_s(38u);
  const auto single_receiver_oscillator_indicator = read_u(1u);
  const auto reserved_b = read_u(1u);
  const auto ecef_y = read_s(38u);
  const auto quarter_cycle_indicator = read_u(2u);
  const auto ecef_z = read_s(38u);

  (void)reserved_a;
  (void)reserved_b;

  if (!parsed_message_type.has_value() || !station_id.has_value() || !itrf_year.has_value() ||
      !gps_indicator.has_value() || !glonass_indicator.has_value() ||
      !galileo_indicator.has_value() || !reference_station_indicator.has_value() ||
      !ecef_x.has_value() || !single_receiver_oscillator_indicator.has_value() ||
      !ecef_y.has_value() || !quarter_cycle_indicator.has_value() || !ecef_z.has_value())
  {
    return ParserResult<RtcmBaseStationArpRecord>::InvalidData();
  }

  RtcmBaseStationArpRecord record;
  record.message_type = static_cast<std::uint16_t>(*parsed_message_type);
  record.station_id = static_cast<std::uint16_t>(*station_id);
  record.itrf_year = static_cast<std::uint8_t>(*itrf_year);
  record.gps_indicator = *gps_indicator != 0u;
  record.glonass_indicator = *glonass_indicator != 0u;
  record.galileo_indicator = *galileo_indicator != 0u;
  record.reference_station_indicator = *reference_station_indicator != 0u;
  record.ecef_x_m = static_cast<double>(*ecef_x) * kRtcmArpCoordinateScaleM;
  record.ecef_y_m = static_cast<double>(*ecef_y) * kRtcmArpCoordinateScaleM;
  record.ecef_z_m = static_cast<double>(*ecef_z) * kRtcmArpCoordinateScaleM;
  record.single_receiver_oscillator_indicator =
      *single_receiver_oscillator_indicator != 0u;
  record.quarter_cycle_indicator = static_cast<std::uint8_t>(*quarter_cycle_indicator);

  if (*message_type == 1006u)
  {
    const auto antenna_height = read_u(16u);
    if (!antenna_height.has_value())
    {
      return ParserResult<RtcmBaseStationArpRecord>::InvalidData();
    }

    record.antenna_height_m =
        static_cast<double>(*antenna_height) * kRtcmArpCoordinateScaleM;
  }

  return ParserResult<RtcmBaseStationArpRecord>::RecordReady(record);
}

ParserResult<RtcmGlonassCodePhaseBiasRecord> ParseRtcmGlonassCodePhaseBias(
    const RtcmFrame& frame)
{
  const std::optional<std::uint16_t> message_type = ExtractRtcmMessageType(frame);
  if (!message_type.has_value() || !IsRtcmGlonassBiasMessage(*message_type))
  {
    return ParserResult<RtcmGlonassCodePhaseBiasRecord>::InvalidData();
  }

  if (frame.payload.size() * 8u < kRtcm1230HeaderBits)
  {
    return ParserResult<RtcmGlonassCodePhaseBiasRecord>::InvalidData();
  }

  std::size_t bit_offset = 0u;
  const auto read_u = [&](const std::size_t bit_count) {
    const auto value = ReadRtcmUnsignedBits(frame.payload, bit_offset, bit_count);
    if (value.has_value())
    {
      bit_offset += bit_count;
    }
    return value;
  };
  const auto read_s = [&](const std::size_t bit_count) {
    const auto value = ReadRtcmSignedBits(frame.payload, bit_offset, bit_count);
    if (value.has_value())
    {
      bit_offset += bit_count;
    }
    return value;
  };

  const auto parsed_message_type = read_u(12u);
  const auto station_id = read_u(12u);
  const auto code_phase_bias_indicator = read_u(1u);
  const auto reserved = read_u(3u);
  const auto has_l1_ca_bias = read_u(1u);
  const auto has_l1_p_bias = read_u(1u);
  const auto has_l2_ca_bias = read_u(1u);
  const auto has_l2_p_bias = read_u(1u);

  (void)reserved;

  if (!parsed_message_type.has_value() || !station_id.has_value() ||
      !code_phase_bias_indicator.has_value() || !has_l1_ca_bias.has_value() ||
      !has_l1_p_bias.has_value() || !has_l2_ca_bias.has_value() ||
      !has_l2_p_bias.has_value())
  {
    return ParserResult<RtcmGlonassCodePhaseBiasRecord>::InvalidData();
  }

  RtcmGlonassCodePhaseBiasRecord record;
  record.message_type = static_cast<std::uint16_t>(*parsed_message_type);
  record.station_id = static_cast<std::uint16_t>(*station_id);
  record.code_phase_bias_indicator = *code_phase_bias_indicator != 0u;
  record.signal_mask = static_cast<std::uint8_t>(
      ((*has_l1_ca_bias != 0u) ? 0x01u : 0x00u) |
      ((*has_l1_p_bias != 0u) ? 0x02u : 0x00u) |
      ((*has_l2_ca_bias != 0u) ? 0x04u : 0x00u) |
      ((*has_l2_p_bias != 0u) ? 0x08u : 0x00u));

  const auto read_optional_bias = [&](const bool present, std::optional<double>& destination) {
    if (!present)
    {
      return true;
    }

    const auto raw_bias = read_s(16u);
    if (!raw_bias.has_value())
    {
      return false;
    }

    destination = static_cast<double>(*raw_bias) * kRtcmGlonassCodePhaseBiasScaleM;
    return true;
  };

  if (!read_optional_bias(*has_l1_ca_bias != 0u, record.l1_ca_bias_m) ||
      !read_optional_bias(*has_l1_p_bias != 0u, record.l1_p_bias_m) ||
      !read_optional_bias(*has_l2_ca_bias != 0u, record.l2_ca_bias_m) ||
      !read_optional_bias(*has_l2_p_bias != 0u, record.l2_p_bias_m))
  {
    return ParserResult<RtcmGlonassCodePhaseBiasRecord>::InvalidData();
  }

  record.has_any_bias_values = record.l1_ca_bias_m.has_value() ||
                               record.l1_p_bias_m.has_value() ||
                               record.l2_ca_bias_m.has_value() ||
                               record.l2_p_bias_m.has_value();
  record.valid = record.code_phase_bias_indicator && record.has_any_bias_values;

  return ParserResult<RtcmGlonassCodePhaseBiasRecord>::RecordReady(record);
}

ParserResult<RtcmMsmSummaryRecord> ParseRtcmMsmSummary(const RtcmFrame& frame)
{
  const std::optional<std::uint16_t> message_type = ExtractRtcmMessageType(frame);
  if (!message_type.has_value() || !IsRtcmMsmMessage(*message_type))
  {
    return ParserResult<RtcmMsmSummaryRecord>::InvalidData();
  }

  if (frame.payload.size() * 8u < kRtcmMsmHeaderBits)
  {
    return ParserResult<RtcmMsmSummaryRecord>::InvalidData();
  }

  std::size_t bit_offset = 0u;
  const auto read_u = [&](const std::size_t bit_count) {
    const auto value = ReadRtcmUnsignedBits(frame.payload, bit_offset, bit_count);
    if (value.has_value())
    {
      bit_offset += bit_count;
    }
    return value;
  };

  const auto parsed_message_type = read_u(12u);
  const auto station_id = read_u(12u);
  const auto epoch_time = read_u(30u);
  const auto multiple_message = read_u(1u);
  const auto issue_of_data_station = read_u(3u);
  const auto session_transmission_time = read_u(7u);
  const auto clock_steering_indicator = read_u(2u);
  const auto external_clock_indicator = read_u(2u);
  const auto divergence_free_smoothing = read_u(1u);
  const auto smoothing_interval = read_u(3u);
  const auto satellite_mask = read_u(64u);
  const auto signal_mask = read_u(32u);

  (void)epoch_time;

  if (!parsed_message_type.has_value() || !station_id.has_value() ||
      !multiple_message.has_value() || !issue_of_data_station.has_value() ||
      !session_transmission_time.has_value() || !clock_steering_indicator.has_value() ||
      !external_clock_indicator.has_value() || !divergence_free_smoothing.has_value() ||
      !smoothing_interval.has_value() || !satellite_mask.has_value() ||
      !signal_mask.has_value())
  {
    return ParserResult<RtcmMsmSummaryRecord>::InvalidData();
  }

  const std::uint8_t satellite_count = CountSetBits(*satellite_mask);
  const std::uint8_t signal_count =
      CountSetBits(static_cast<std::uint64_t>(*signal_mask));
  const std::size_t cell_mask_bits =
      static_cast<std::size_t>(satellite_count) * static_cast<std::size_t>(signal_count);
  const auto cell_count = CountSetBitsInRange(frame.payload, bit_offset, cell_mask_bits);
  if (!cell_count.has_value())
  {
    return ParserResult<RtcmMsmSummaryRecord>::InvalidData();
  }

  RtcmMsmSummaryRecord record;
  record.message_type = static_cast<std::uint16_t>(*parsed_message_type);
  record.station_id = static_cast<std::uint16_t>(*station_id);
  record.constellation = GetRtcmMsmConstellation(record.message_type);
  record.msm_variant = GetRtcmMsmVariant(record.message_type);
  record.multiple_message = *multiple_message != 0u;
  record.issue_of_data_station = static_cast<std::uint8_t>(*issue_of_data_station);
  record.session_transmission_time =
      static_cast<std::uint8_t>(*session_transmission_time);
  record.clock_steering_indicator =
      static_cast<std::uint8_t>(*clock_steering_indicator);
  record.external_clock_indicator =
      static_cast<std::uint8_t>(*external_clock_indicator);
  record.divergence_free_smoothing = *divergence_free_smoothing != 0u;
  record.smoothing_interval = static_cast<std::uint8_t>(*smoothing_interval);
  record.satellite_count = satellite_count;
  record.signal_count = signal_count;
  record.cell_count = *cell_count;

  return ParserResult<RtcmMsmSummaryRecord>::RecordReady(record);
}

}  // namespace universal_gnss_protocols
