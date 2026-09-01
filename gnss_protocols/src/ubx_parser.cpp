#include "universal_gnss_protocols/ubx_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace universal_gnss_protocols
{

namespace
{

constexpr std::uint8_t kUbxNavClass = 0x01u;
constexpr std::uint8_t kUbxRxmClass = 0x02u;
constexpr std::uint8_t kUbxAckClass = 0x05u;
constexpr std::uint8_t kUbxMonClass = 0x0Au;
constexpr std::uint8_t kUbxAckNakId = 0x00u;
constexpr std::uint8_t kUbxAckAckId = 0x01u;
constexpr std::uint8_t kUbxRxmRtcmId = 0x32u;
constexpr std::uint8_t kUbxNavStatusId = 0x03u;
constexpr std::uint8_t kUbxNavDopId = 0x04u;
constexpr std::uint8_t kUbxNavPvtId = 0x07u;
constexpr std::uint8_t kUbxNavSatId = 0x35u;
constexpr std::uint8_t kUbxMonHwId = 0x09u;
constexpr std::uint8_t kUbxMonHw2Id = 0x0Bu;
constexpr std::uint8_t kUbxMonRfId = 0x38u;
constexpr std::size_t kUbxAckPayloadSize = 2u;
constexpr std::size_t kUbxRxmRtcmPayloadSize = 8u;
constexpr std::size_t kUbxNavStatusPayloadSize = 16u;
constexpr std::size_t kUbxNavDopPayloadSize = 18u;
constexpr std::size_t kUbxNavPvtPayloadSize = 92u;
constexpr std::size_t kUbxNavSatHeaderSize = 8u;
constexpr std::size_t kUbxNavSatBlockSize = 12u;
constexpr std::size_t kUbxMonHwClassicPayloadSize = 60u;
constexpr std::size_t kUbxMonHwReservedPayloadSize = 56u;
constexpr std::size_t kUbxMonHw2PayloadSize = 28u;
constexpr std::size_t kUbxMonRfHeaderSize = 4u;
constexpr std::size_t kUbxMonRfBlockSize = 24u;

constexpr std::uint8_t kValidDateBit = 1u << 0;
constexpr std::uint8_t kValidTimeBit = 1u << 1;
constexpr std::uint8_t kFullyResolvedBit = 1u << 2;

constexpr std::uint8_t kGnssFixOkBit = 1u << 0;
constexpr std::uint8_t kDiffSolnBit = 1u << 1;
constexpr std::uint8_t kHeadVehValidBit = 1u << 5;
constexpr std::uint8_t kCarrSolnValidBit = 1u << 1;
constexpr std::uint8_t kCarrSolnMask = 0xC0u;
constexpr std::uint8_t kRxmRtcmCrcFailedBit = 1u << 0;
constexpr std::uint8_t kRxmRtcmMsgUsedMask = 0x06u;
constexpr std::uint8_t kMonHwRtcCalibBit = 1u << 0;
constexpr std::uint8_t kMonHwSafeBootBit = 1u << 1;
constexpr std::uint8_t kMonHwJammingMask = 0x0Cu;
constexpr std::uint8_t kMonHwXtalAbsentBit = 1u << 4;

constexpr std::uint16_t kInvalidLlhBit = 1u << 0;
constexpr std::uint32_t kNavSatQualityMask = 0x00000007u;
constexpr std::uint32_t kNavSatUsedBit = 1u << 3;
constexpr std::uint32_t kNavSatHealthMask = 0x00000030u;
constexpr std::uint8_t kMonRfJammingMask = 0x03u;

std::uint16_t ReadLeU2(const ByteVector& payload, std::size_t offset)
{
  return static_cast<std::uint16_t>(payload[offset]) |
         (static_cast<std::uint16_t>(payload[offset + 1u]) << 8);
}

std::uint32_t ReadLeU4(const ByteVector& payload, std::size_t offset)
{
  return static_cast<std::uint32_t>(payload[offset]) |
         (static_cast<std::uint32_t>(payload[offset + 1u]) << 8) |
         (static_cast<std::uint32_t>(payload[offset + 2u]) << 16) |
         (static_cast<std::uint32_t>(payload[offset + 3u]) << 24);
}

std::int32_t ReadLeI4(const ByteVector& payload, std::size_t offset)
{
  return static_cast<std::int32_t>(ReadLeU4(payload, offset));
}

std::int16_t ReadLeI2(const ByteVector& payload, std::size_t offset)
{
  return static_cast<std::int16_t>(ReadLeU2(payload, offset));
}

float ScaleMillimetersToMeters(std::int32_t millimeters)
{
  return static_cast<float>(millimeters) / 1000.0f;
}

float ScaleMillimetersToMeters(std::uint32_t millimeters)
{
  return static_cast<float>(millimeters) / 1000.0f;
}

double ScaleMillimetersToMetersDouble(std::int32_t millimeters)
{
  return static_cast<double>(millimeters) / 1000.0;
}

float ScaleHeading1e5ToDegrees(std::int32_t scaled_heading)
{
  return static_cast<float>(scaled_heading) * 1e-5f;
}

float ScaleDop1e2ToUnit(std::uint16_t scaled_dop)
{
  return static_cast<float>(scaled_dop) * 0.01f;
}

UbxCarrierSolutionStatus DecodeCarrierSolution(std::uint8_t flags)
{
  switch ((flags & kCarrSolnMask) >> 6)
  {
    case 1u:
      return UbxCarrierSolutionStatus::kFloat;
    case 2u:
      return UbxCarrierSolutionStatus::kFixed;
    default:
      return UbxCarrierSolutionStatus::kNone;
  }
}

std::optional<bool> DecodeNavSatHealth(std::uint32_t flags)
{
  switch ((flags & kNavSatHealthMask) >> 4)
  {
    case 1u:
      return true;
    case 2u:
      return false;
    default:
      return std::nullopt;
  }
}

UbxMonRfJammingState DecodeMonRfJammingState(std::uint8_t flags)
{
  switch (flags & kMonRfJammingMask)
  {
    case 1u:
      return UbxMonRfJammingState::kOk;
    case 2u:
      return UbxMonRfJammingState::kWarning;
    case 3u:
      return UbxMonRfJammingState::kCritical;
    default:
      return UbxMonRfJammingState::kUnknown;
  }
}

std::optional<UbxAntennaStatus> DecodeAntennaStatus(const std::uint8_t raw_value)
{
  switch (raw_value)
  {
    case 0u:
      return UbxAntennaStatus::kInit;
    case 1u:
      return UbxAntennaStatus::kDontKnow;
    case 2u:
      return UbxAntennaStatus::kOk;
    case 3u:
      return UbxAntennaStatus::kShort;
    case 4u:
      return UbxAntennaStatus::kOpen;
    default:
      return std::nullopt;
  }
}

std::optional<UbxAntennaPower> DecodeAntennaPower(const std::uint8_t raw_value)
{
  switch (raw_value)
  {
    case 0u:
      return UbxAntennaPower::kOff;
    case 1u:
      return UbxAntennaPower::kOn;
    case 2u:
      return UbxAntennaPower::kDontKnow;
    default:
      return std::nullopt;
  }
}

UbxMonRfJammingState DecodeMonHwJammingState(const std::uint8_t flags)
{
  return DecodeMonRfJammingState(static_cast<std::uint8_t>((flags & kMonHwJammingMask) >> 2u));
}

universal_gnss::GnssDiagnosticEvent BuildReceiverDiagnostic(
    const universal_gnss::GnssDiagnosticSeverity severity,
    const std::string& code,
    const std::string& message,
    const std::optional<ProtocolTimestampNs>& timestamp_ns,
    const std::string& source)
{
  universal_gnss::GnssDiagnosticEvent event;
  event.severity = severity;
  event.category = universal_gnss::GnssDiagnosticCategory::kReceiver;
  event.code = code;
  event.message = message;
  event.timestamp_ns = timestamp_ns;
  event.source = source;
  return event;
}

UbxRxmRtcmMessageUse DecodeRxmRtcmMessageUse(const std::uint8_t flags)
{
  switch ((flags & kRxmRtcmMsgUsedMask) >> 1)
  {
    case 1u:
      return UbxRxmRtcmMessageUse::kNotUsed;
    case 2u:
      return UbxRxmRtcmMessageUse::kUsed;
    default:
      return UbxRxmRtcmMessageUse::kUnknown;
  }
}

std::string FormatRtcmTypeMessage(const std::uint16_t message_type)
{
  return "RTCM " + std::to_string(message_type);
}

std::string FormatRtcmTypeAndStationMessage(const UbxRxmRtcmRecord& record)
{
  std::string message = FormatRtcmTypeMessage(record.message_type);
  if (record.ref_station_id != 0xFFFFu)
  {
    message += " from ref station " + std::to_string(record.ref_station_id);
  }
  return message;
}

void SetCorrectionState(universal_gnss::GnssRuntimeState& state,
                        const bool differential_solution)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kDifferentialCorrections);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kCorrectionsActive);
  universal_gnss::SetOptionalValue(state,
                                   universal_gnss::GnssCapability::kDifferentialCorrections,
                                   state.differential_corrections,
                                   differential_solution);
  universal_gnss::SetOptionalValue(state,
                                   universal_gnss::GnssCapability::kCorrectionsActive,
                                   state.corrections_active,
                                   differential_solution);
}

void ClearPositionSolutionValues(universal_gnss::GnssRuntimeState& state)
{
  universal_gnss::ClearPositionValues(state);

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHorizontalAccuracy);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kVerticalAccuracy);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHeading);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHeadingAccuracy);
  universal_gnss::ClearOptionalValue(state,
                                     universal_gnss::GnssCapability::kHorizontalAccuracy,
                                     state.horizontal_accuracy_m);
  universal_gnss::ClearOptionalValue(state,
                                     universal_gnss::GnssCapability::kVerticalAccuracy,
                                     state.vertical_accuracy_m);
  universal_gnss::ClearOptionalValue(
      state, universal_gnss::GnssCapability::kHeading, state.heading_deg);
  universal_gnss::ClearOptionalValue(state,
                                     universal_gnss::GnssCapability::kHeadingAccuracy,
                                     state.heading_accuracy_deg);
}

}  // namespace

ParserResult<UbxAckRecord> ParseUbxAck(const UbxFrame& frame)
{
  if (frame.class_id != kUbxAckClass ||
      (frame.message_id != kUbxAckNakId && frame.message_id != kUbxAckAckId))
  {
    return ParserResult<UbxAckRecord>::Skipped();
  }
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    return ParserResult<UbxAckRecord>::InvalidData();
  }
  if (frame.payload.size() != kUbxAckPayloadSize)
  {
    return ParserResult<UbxAckRecord>::InvalidData();
  }

  UbxAckRecord record;
  record.timestamp_ns = frame.timestamp_ns;
  record.kind = frame.message_id == kUbxAckAckId ? UbxAckMessageKind::kAck
                                                 : UbxAckMessageKind::kNak;
  record.target_class_id = frame.payload[0u];
  record.target_message_id = frame.payload[1u];
  return ParserResult<UbxAckRecord>::RecordReady(std::move(record));
}

ParserResult<UbxRxmRtcmRecord> ParseUbxRxmRtcm(const UbxFrame& frame)
{
  if (frame.class_id != kUbxRxmClass || frame.message_id != kUbxRxmRtcmId)
  {
    return ParserResult<UbxRxmRtcmRecord>::Skipped();
  }
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    return ParserResult<UbxRxmRtcmRecord>::InvalidData();
  }
  if (frame.payload.size() != kUbxRxmRtcmPayloadSize)
  {
    return ParserResult<UbxRxmRtcmRecord>::InvalidData();
  }
  if (frame.payload[0u] != 0x02u)
  {
    return ParserResult<UbxRxmRtcmRecord>::InvalidData();
  }

  UbxRxmRtcmRecord record;
  record.timestamp_ns = frame.timestamp_ns;
  record.version = frame.payload[0u];
  record.flags = frame.payload[1u];
  record.crc_failed = (record.flags & kRxmRtcmCrcFailedBit) != 0u;
  record.crc_ok = !record.crc_failed;
  record.message_use = DecodeRxmRtcmMessageUse(record.flags);
  record.message_used = record.message_use == UbxRxmRtcmMessageUse::kUsed;
  record.message_use_known = record.message_use != UbxRxmRtcmMessageUse::kUnknown;
  record.sub_type = ReadLeU2(frame.payload, 2u);
  record.ref_station_id = ReadLeU2(frame.payload, 4u);
  record.message_type = ReadLeU2(frame.payload, 6u);
  return ParserResult<UbxRxmRtcmRecord>::RecordReady(std::move(record));
}

ParserResult<UbxNavStatusRecord> ParseUbxNavStatus(const UbxFrame& frame)
{
  if (frame.class_id != kUbxNavClass || frame.message_id != kUbxNavStatusId)
  {
    return ParserResult<UbxNavStatusRecord>::Skipped();
  }
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    return ParserResult<UbxNavStatusRecord>::InvalidData();
  }
  if (frame.payload.size() != kUbxNavStatusPayloadSize)
  {
    return ParserResult<UbxNavStatusRecord>::InvalidData();
  }

  UbxNavStatusRecord record;
  record.timestamp_ns = frame.timestamp_ns;
  record.i_tow_ms = ReadLeU4(frame.payload, 0u);
  record.gps_fix = static_cast<UbxNavStatusFixType>(frame.payload[4u]);
  record.flags = frame.payload[5u];
  record.fix_stat = frame.payload[6u];
  record.flags2 = frame.payload[7u];
  record.ttff_ms = ReadLeU4(frame.payload, 8u);
  record.msss_ms = ReadLeU4(frame.payload, 12u);

  record.gnss_fix_ok = (record.flags & kGnssFixOkBit) != 0u;
  record.differential_solution = (record.flags & kDiffSolnBit) != 0u;
  record.carrier_solution_valid = (record.fix_stat & kCarrSolnValidBit) != 0u;
  record.carrier_solution = DecodeCarrierSolution(record.flags2);

  return ParserResult<UbxNavStatusRecord>::RecordReady(std::move(record));
}

ParserResult<UbxNavPvtRecord> ParseUbxNavPvt(const UbxFrame& frame)
{
  if (frame.class_id != kUbxNavClass || frame.message_id != kUbxNavPvtId)
  {
    return ParserResult<UbxNavPvtRecord>::Skipped();
  }
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    return ParserResult<UbxNavPvtRecord>::InvalidData();
  }
  if (frame.payload.size() != kUbxNavPvtPayloadSize)
  {
    return ParserResult<UbxNavPvtRecord>::InvalidData();
  }

  UbxNavPvtRecord record;
  record.timestamp_ns = frame.timestamp_ns;
  record.i_tow_ms = ReadLeU4(frame.payload, 0u);
  record.year = ReadLeU2(frame.payload, 4u);
  record.month = frame.payload[6u];
  record.day = frame.payload[7u];
  record.hour = frame.payload[8u];
  record.minute = frame.payload[9u];
  record.second = frame.payload[10u];

  const std::uint8_t valid = frame.payload[11u];
  record.valid_date = (valid & kValidDateBit) != 0u;
  record.valid_time = (valid & kValidTimeBit) != 0u;
  record.fully_resolved_time = (valid & kFullyResolvedBit) != 0u;
  record.nano_ns = ReadLeI4(frame.payload, 16u);

  record.fix_type = static_cast<UbxNavPvtFixType>(frame.payload[20u]);
  record.flags = frame.payload[21u];
  record.flags2 = frame.payload[22u];
  record.flags3 = ReadLeU2(frame.payload, 78u);

  record.gnss_fix_ok = (record.flags & kGnssFixOkBit) != 0u;
  record.differential_solution = (record.flags & kDiffSolnBit) != 0u;
  record.heading_vehicle_valid = (record.flags & kHeadVehValidBit) != 0u;
  record.invalid_llh = (record.flags3 & kInvalidLlhBit) != 0u;
  record.carrier_solution = DecodeCarrierSolution(record.flags);

  record.num_sv = frame.payload[23u];
  record.longitude_deg = static_cast<double>(ReadLeI4(frame.payload, 24u)) * 1e-7;
  record.latitude_deg = static_cast<double>(ReadLeI4(frame.payload, 28u)) * 1e-7;
  record.height_ellipsoid_m = ScaleMillimetersToMetersDouble(ReadLeI4(frame.payload, 32u));
  record.height_msl_m = ScaleMillimetersToMetersDouble(ReadLeI4(frame.payload, 36u));
  record.horizontal_accuracy_m = ScaleMillimetersToMeters(ReadLeU4(frame.payload, 40u));
  record.vertical_accuracy_m = ScaleMillimetersToMeters(ReadLeU4(frame.payload, 44u));

  record.vel_north_mm_s = ReadLeI4(frame.payload, 48u);
  record.vel_east_mm_s = ReadLeI4(frame.payload, 52u);
  record.vel_down_mm_s = ReadLeI4(frame.payload, 56u);
  record.ground_speed_mm_s = ReadLeI4(frame.payload, 60u);
  record.course_over_ground_deg = ScaleHeading1e5ToDegrees(ReadLeI4(frame.payload, 64u));
  record.heading_accuracy_deg = static_cast<float>(ReadLeU4(frame.payload, 72u)) * 1e-5f;
  record.heading_vehicle_deg = ScaleHeading1e5ToDegrees(ReadLeI4(frame.payload, 84u));

  return ParserResult<UbxNavPvtRecord>::RecordReady(std::move(record));
}

ParserResult<UbxNavDopRecord> ParseUbxNavDop(const UbxFrame& frame)
{
  if (frame.class_id != kUbxNavClass || frame.message_id != kUbxNavDopId)
  {
    return ParserResult<UbxNavDopRecord>::Skipped();
  }
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    return ParserResult<UbxNavDopRecord>::InvalidData();
  }
  if (frame.payload.size() != kUbxNavDopPayloadSize)
  {
    return ParserResult<UbxNavDopRecord>::InvalidData();
  }

  UbxNavDopRecord record;
  record.timestamp_ns = frame.timestamp_ns;
  record.i_tow_ms = ReadLeU4(frame.payload, 0u);
  record.g_dop = ScaleDop1e2ToUnit(ReadLeU2(frame.payload, 4u));
  record.p_dop = ScaleDop1e2ToUnit(ReadLeU2(frame.payload, 6u));
  record.t_dop = ScaleDop1e2ToUnit(ReadLeU2(frame.payload, 8u));
  record.v_dop = ScaleDop1e2ToUnit(ReadLeU2(frame.payload, 10u));
  record.h_dop = ScaleDop1e2ToUnit(ReadLeU2(frame.payload, 12u));
  record.n_dop = ScaleDop1e2ToUnit(ReadLeU2(frame.payload, 14u));
  record.e_dop = ScaleDop1e2ToUnit(ReadLeU2(frame.payload, 16u));
  return ParserResult<UbxNavDopRecord>::RecordReady(std::move(record));
}

ParserResult<UbxNavSatRecord> ParseUbxNavSat(const UbxFrame& frame)
{
  if (frame.class_id != kUbxNavClass || frame.message_id != kUbxNavSatId)
  {
    return ParserResult<UbxNavSatRecord>::Skipped();
  }
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    return ParserResult<UbxNavSatRecord>::InvalidData();
  }
  if (frame.payload.size() < kUbxNavSatHeaderSize)
  {
    return ParserResult<UbxNavSatRecord>::InvalidData();
  }

  const std::uint8_t version = frame.payload[4u];
  const std::uint8_t num_svs = frame.payload[5u];
  if (version != 0x01u)
  {
    return ParserResult<UbxNavSatRecord>::InvalidData();
  }

  const std::size_t expected_payload_size =
      kUbxNavSatHeaderSize + (static_cast<std::size_t>(num_svs) * kUbxNavSatBlockSize);
  if (frame.payload.size() != expected_payload_size)
  {
    return ParserResult<UbxNavSatRecord>::InvalidData();
  }
  if (num_svs > UbxNavSatRecord::kMaxSatellites)
  {
    return ParserResult<UbxNavSatRecord>::Overflow();
  }

  UbxNavSatRecord record;
  record.timestamp_ns = frame.timestamp_ns;
  record.i_tow_ms = ReadLeU4(frame.payload, 0u);
  record.version = version;
  record.num_svs = num_svs;

  for (std::size_t index = 0; index < static_cast<std::size_t>(num_svs); ++index)
  {
    const std::size_t offset = kUbxNavSatHeaderSize + (index * kUbxNavSatBlockSize);
    const std::uint32_t flags = ReadLeU4(frame.payload, offset + 8u);

    UbxNavSatSatellite satellite;
    satellite.gnss_id = frame.payload[offset];
    satellite.sv_id = frame.payload[offset + 1u];
    satellite.cno_db_hz = frame.payload[offset + 2u];
    satellite.elevation_deg = static_cast<std::int8_t>(frame.payload[offset + 3u]);
    satellite.azimuth_deg = ReadLeI2(frame.payload, offset + 4u);
    satellite.quality_indicator = static_cast<std::uint8_t>(flags & kNavSatQualityMask);
    satellite.used_in_navigation = (flags & kNavSatUsedBit) != 0u;
    satellite.healthy = DecodeNavSatHealth(flags);

    record.satellites[index] = satellite;
    ++record.satellite_count;
    if (satellite.used_in_navigation && record.used_satellite_count < num_svs)
    {
      ++record.used_satellite_count;
    }
  }

  return ParserResult<UbxNavSatRecord>::RecordReady(std::move(record));
}

ParserResult<UbxMonHwRecord> ParseUbxMonHw(const UbxFrame& frame)
{
  if (frame.class_id != kUbxMonClass || frame.message_id != kUbxMonHwId)
  {
    return ParserResult<UbxMonHwRecord>::Skipped();
  }
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    return ParserResult<UbxMonHwRecord>::InvalidData();
  }
  if (frame.payload.size() != kUbxMonHwClassicPayloadSize &&
      frame.payload.size() != kUbxMonHwReservedPayloadSize)
  {
    return ParserResult<UbxMonHwRecord>::InvalidData();
  }

  UbxMonHwRecord record;
  record.timestamp_ns = frame.timestamp_ns;
  record.payload_size = frame.payload.size();

  if (frame.payload.size() == kUbxMonHwReservedPayloadSize)
  {
    record.layout = UbxMonHwLayout::kReserved;
    return ParserResult<UbxMonHwRecord>::RecordReady(std::move(record));
  }

  record.layout = UbxMonHwLayout::kClassic;
  record.noise_per_ms = ReadLeU2(frame.payload, 16u);
  record.agc_count = ReadLeU2(frame.payload, 18u);
  record.antenna_status = DecodeAntennaStatus(frame.payload[20u]);
  record.antenna_power = DecodeAntennaPower(frame.payload[21u]);
  record.flags = frame.payload[22u];
  record.jamming_state = DecodeMonHwJammingState(*record.flags);
  record.rtc_calibrated = (*record.flags & kMonHwRtcCalibBit) != 0u;
  record.safe_boot = (*record.flags & kMonHwSafeBootBit) != 0u;
  record.xtal_absent = (*record.flags & kMonHwXtalAbsentBit) != 0u;
  record.cw_suppression = frame.payload[45u];
  return ParserResult<UbxMonHwRecord>::RecordReady(std::move(record));
}

ParserResult<UbxMonHw2Record> ParseUbxMonHw2(const UbxFrame& frame)
{
  if (frame.class_id != kUbxMonClass || frame.message_id != kUbxMonHw2Id)
  {
    return ParserResult<UbxMonHw2Record>::Skipped();
  }
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    return ParserResult<UbxMonHw2Record>::InvalidData();
  }
  if (frame.payload.size() != kUbxMonHw2PayloadSize)
  {
    return ParserResult<UbxMonHw2Record>::InvalidData();
  }

  UbxMonHw2Record record;
  record.timestamp_ns = frame.timestamp_ns;
  record.ofs_i = static_cast<std::int8_t>(frame.payload[0u]);
  record.mag_i = frame.payload[1u];
  record.ofs_q = static_cast<std::int8_t>(frame.payload[2u]);
  record.mag_q = frame.payload[3u];
  record.cfg_source = frame.payload[4u];
  record.low_level_configuration = ReadLeU4(frame.payload, 8u);
  record.post_status = ReadLeU4(frame.payload, 20u);
  return ParserResult<UbxMonHw2Record>::RecordReady(std::move(record));
}

ParserResult<UbxMonRfRecord> ParseUbxMonRf(const UbxFrame& frame)
{
  if (frame.class_id != kUbxMonClass || frame.message_id != kUbxMonRfId)
  {
    return ParserResult<UbxMonRfRecord>::Skipped();
  }
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    return ParserResult<UbxMonRfRecord>::InvalidData();
  }
  if (frame.payload.size() < kUbxMonRfHeaderSize)
  {
    return ParserResult<UbxMonRfRecord>::InvalidData();
  }

  const std::uint8_t version = frame.payload[0u];
  const std::uint8_t block_count = frame.payload[1u];
  if (version != 0x00u)
  {
    return ParserResult<UbxMonRfRecord>::InvalidData();
  }

  const std::size_t expected_payload_size =
      kUbxMonRfHeaderSize + (static_cast<std::size_t>(block_count) * kUbxMonRfBlockSize);
  if (frame.payload.size() != expected_payload_size)
  {
    return ParserResult<UbxMonRfRecord>::InvalidData();
  }
  if (block_count > UbxMonRfRecord::kMaxBlocks)
  {
    return ParserResult<UbxMonRfRecord>::Overflow();
  }

  UbxMonRfRecord record;
  record.timestamp_ns = frame.timestamp_ns;
  record.version = version;
  record.block_count = block_count;

  for (std::size_t index = 0; index < static_cast<std::size_t>(block_count); ++index)
  {
    const std::size_t offset = kUbxMonRfHeaderSize + (index * kUbxMonRfBlockSize);
    UbxMonRfBlock block;
    block.block_id = frame.payload[offset];
    block.flags = frame.payload[offset + 1u];
    block.jamming_state = DecodeMonRfJammingState(block.flags);
    block.antenna_status = frame.payload[offset + 2u];
    block.antenna_power = frame.payload[offset + 3u];
    block.post_status = ReadLeU4(frame.payload, offset + 4u);
    block.noise_per_ms = ReadLeU2(frame.payload, offset + 12u);
    block.agc_count = ReadLeU2(frame.payload, offset + 14u);
    block.cw_suppression = frame.payload[offset + 16u];
    record.blocks[index] = block;
  }

  return ParserResult<UbxMonRfRecord>::RecordReady(std::move(record));
}

universal_gnss::GnssDiagnosticEvent UbxRxmRtcmToDiagnosticEvent(
    const UbxRxmRtcmRecord& record)
{
  using universal_gnss::GnssDiagnosticCategory;
  using universal_gnss::GnssDiagnosticEvent;
  using universal_gnss::GnssDiagnosticSeverity;

  GnssDiagnosticEvent event;
  event.category = GnssDiagnosticCategory::kCorrection;
  event.timestamp_ns = record.timestamp_ns;
  event.source = std::string("ubx.rxm_rtcm");

  if (record.crc_failed)
  {
    event.severity = GnssDiagnosticSeverity::kWarning;
    event.code = "ubx_rxm_rtcm.crc_failed";
    event.message =
        FormatRtcmTypeAndStationMessage(record) +
        " failed receiver-side CRC validation";
    return event;
  }

  if (record.message_use == UbxRxmRtcmMessageUse::kUsed)
  {
    event.severity = GnssDiagnosticSeverity::kOk;
    event.code = "ubx_rxm_rtcm.accepted";
    event.message =
        FormatRtcmTypeAndStationMessage(record) +
        " was accepted by the receiver";
    return event;
  }

  if (record.message_use == UbxRxmRtcmMessageUse::kNotUsed)
  {
    event.severity = GnssDiagnosticSeverity::kWarning;
    event.code = "ubx_rxm_rtcm.not_used";
    event.message =
        FormatRtcmTypeAndStationMessage(record) +
        " was received but not used by the receiver";
    return event;
  }

  event.severity = GnssDiagnosticSeverity::kInfo;
  event.code = "ubx_rxm_rtcm.usage_unknown";
  event.message =
      FormatRtcmTypeAndStationMessage(record) +
      " was parsed by the receiver but usage is unknown";
  return event;
}

universal_gnss::GnssDiagnosticEvents UbxMonHwToDiagnosticEvents(const UbxMonHwRecord& record)
{
  using universal_gnss::GnssDiagnosticEvents;
  using universal_gnss::GnssDiagnosticSeverity;

  GnssDiagnosticEvents events;
  if (record.layout != UbxMonHwLayout::kClassic)
  {
    return events;
  }

  if (record.antenna_status.has_value())
  {
    switch (*record.antenna_status)
    {
      case UbxAntennaStatus::kOk:
      {
        const bool powered =
            record.antenna_power.has_value() && *record.antenna_power == UbxAntennaPower::kOn;
        if (powered)
        {
          events.push_back(BuildReceiverDiagnostic(GnssDiagnosticSeverity::kOk,
                                                   "ubx_mon_hw.antenna_ok",
                                                   "u-blox antenna supervisor reports antenna OK and powered",
                                                   record.timestamp_ns,
                                                   "ubx.mon_hw"));
        }
        else
        {
          events.push_back(BuildReceiverDiagnostic(GnssDiagnosticSeverity::kInfo,
                                                   "ubx_mon_hw.antenna_ok_power_unknown",
                                                   "u-blox antenna supervisor reports antenna OK but antenna power is not confirmed on",
                                                   record.timestamp_ns,
                                                   "ubx.mon_hw"));
        }
        break;
      }
      case UbxAntennaStatus::kOpen:
        events.push_back(BuildReceiverDiagnostic(GnssDiagnosticSeverity::kWarning,
                                                 "ubx_mon_hw.antenna_open",
                                                 "u-blox antenna supervisor reports an open antenna condition",
                                                 record.timestamp_ns,
                                                 "ubx.mon_hw"));
        break;
      case UbxAntennaStatus::kShort:
        events.push_back(BuildReceiverDiagnostic(GnssDiagnosticSeverity::kError,
                                                 "ubx_mon_hw.antenna_short",
                                                 "u-blox antenna supervisor reports an antenna short condition",
                                                 record.timestamp_ns,
                                                 "ubx.mon_hw"));
        break;
      case UbxAntennaStatus::kInit:
      case UbxAntennaStatus::kDontKnow:
        events.push_back(BuildReceiverDiagnostic(GnssDiagnosticSeverity::kUnknown,
                                                 "ubx_mon_hw.antenna_unknown",
                                                 "u-blox antenna supervisor state is not yet known",
                                                 record.timestamp_ns,
                                                 "ubx.mon_hw"));
        break;
    }
  }

  if (record.antenna_power.has_value() &&
      *record.antenna_power == UbxAntennaPower::kOff &&
      record.antenna_status.has_value() &&
      *record.antenna_status != UbxAntennaStatus::kInit &&
      *record.antenna_status != UbxAntennaStatus::kDontKnow)
  {
    events.push_back(BuildReceiverDiagnostic(GnssDiagnosticSeverity::kWarning,
                                             "ubx_mon_hw.antenna_power_off",
                                             "u-blox antenna supervisor reports antenna power off",
                                             record.timestamp_ns,
                                             "ubx.mon_hw"));
  }

  switch (record.jamming_state)
  {
    case UbxMonRfJammingState::kOk:
      events.push_back(BuildReceiverDiagnostic(GnssDiagnosticSeverity::kOk,
                                               "ubx_mon_hw.jamming_ok",
                                               "u-blox hardware monitor reports no significant jamming",
                                               record.timestamp_ns,
                                               "ubx.mon_hw"));
      break;
    case UbxMonRfJammingState::kWarning:
      events.push_back(BuildReceiverDiagnostic(GnssDiagnosticSeverity::kWarning,
                                               "ubx_mon_hw.jamming_warning",
                                               "u-blox hardware monitor reports visible interference but fix remains available",
                                               record.timestamp_ns,
                                               "ubx.mon_hw"));
      break;
    case UbxMonRfJammingState::kCritical:
      events.push_back(BuildReceiverDiagnostic(GnssDiagnosticSeverity::kError,
                                               "ubx_mon_hw.jamming_critical",
                                               "u-blox hardware monitor reports critical interference with no fix",
                                               record.timestamp_ns,
                                               "ubx.mon_hw"));
      break;
    case UbxMonRfJammingState::kUnknown:
    default:
      break;
  }

  return events;
}

universal_gnss::GnssRuntimeState UbxNavStatusToRuntimeState(const UbxNavStatusRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.timestamp_ns;

  switch (record.gps_fix)
  {
    case UbxNavStatusFixType::kNoFix:
      state.fix_valid = false;
      state.fix_type = universal_gnss::GnssFixType::kNoFix;
      break;
    case UbxNavStatusFixType::kDeadReckoningOnly:
      state.fix_valid = false;
      state.fix_type = universal_gnss::GnssFixType::kDeadReckoning;
      break;
    case UbxNavStatusFixType::k2D:
    case UbxNavStatusFixType::k3D:
      state.fix_valid = record.gnss_fix_ok;
      state.fix_type = record.gnss_fix_ok ? universal_gnss::GnssFixType::kFix
                                          : universal_gnss::GnssFixType::kNoFix;
      break;
    case UbxNavStatusFixType::kGnssDeadReckoningCombined:
      state.fix_valid = record.gnss_fix_ok;
      state.fix_type = record.gnss_fix_ok ? universal_gnss::GnssFixType::kFix
                                          : universal_gnss::GnssFixType::kDeadReckoning;
      break;
    case UbxNavStatusFixType::kTimeOnly:
      state.fix_valid = false;
      state.fix_type = universal_gnss::GnssFixType::kNoFix;
      break;
    default:
      state.fix_valid = false;
      state.fix_type = universal_gnss::GnssFixType::kUnknown;
      break;
  }

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kRtkMode);
  SetCorrectionState(state, record.differential_solution);
  if (state.fix_type != universal_gnss::GnssFixType::kUnknown && !state.fix_valid)
  {
    ClearPositionSolutionValues(state);
  }
  if (!record.carrier_solution_valid)
  {
    return state;
  }

  switch (record.carrier_solution)
  {
    case UbxCarrierSolutionStatus::kFloat:
      universal_gnss::SetOptionalValue(
          state,
          universal_gnss::GnssCapability::kRtkMode,
          state.rtk_mode,
          universal_gnss::GnssRtkMode::kFloat);
      break;
    case UbxCarrierSolutionStatus::kFixed:
      universal_gnss::SetOptionalValue(
          state,
          universal_gnss::GnssCapability::kRtkMode,
          state.rtk_mode,
          universal_gnss::GnssRtkMode::kFixed);
      break;
    case UbxCarrierSolutionStatus::kNone:
    default:
      universal_gnss::SetOptionalValue(
          state,
          universal_gnss::GnssCapability::kRtkMode,
          state.rtk_mode,
          universal_gnss::GnssRtkMode::kNone);
      break;
  }

  return state;
}

universal_gnss::GnssRuntimeState UbxNavPvtToRuntimeState(const UbxNavPvtRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.timestamp_ns;

  const bool position_valid =
      !record.invalid_llh &&
      (record.fix_type == UbxNavPvtFixType::k2D ||
       record.fix_type == UbxNavPvtFixType::k3D ||
       record.fix_type == UbxNavPvtFixType::kGnssDeadReckoningCombined) &&
      record.gnss_fix_ok;

  switch (record.fix_type)
  {
    case UbxNavPvtFixType::kNoFix:
      state.fix_valid = false;
      state.fix_type = universal_gnss::GnssFixType::kNoFix;
      break;
    case UbxNavPvtFixType::kDeadReckoningOnly:
      state.fix_valid = false;
      state.fix_type = universal_gnss::GnssFixType::kDeadReckoning;
      break;
    case UbxNavPvtFixType::k2D:
    case UbxNavPvtFixType::k3D:
      state.fix_valid = position_valid;
      state.fix_type = position_valid ? universal_gnss::GnssFixType::kFix
                                      : universal_gnss::GnssFixType::kNoFix;
      break;
    case UbxNavPvtFixType::kGnssDeadReckoningCombined:
      state.fix_valid = position_valid;
      state.fix_type = position_valid ? universal_gnss::GnssFixType::kFix
                                      : universal_gnss::GnssFixType::kDeadReckoning;
      break;
    case UbxNavPvtFixType::kTimeOnly:
      state.fix_valid = false;
      state.fix_type = universal_gnss::GnssFixType::kNoFix;
      break;
    default:
      state.fix_valid = false;
      state.fix_type = universal_gnss::GnssFixType::kUnknown;
      break;
  }

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kRtkMode);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHorizontalAccuracy);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kVerticalAccuracy);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesUsed);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kUtcDate);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kUtcTime);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSpeedOverGround);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kCourseOverGround);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHeading);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHeadingAccuracy);

  switch (record.carrier_solution)
  {
    case UbxCarrierSolutionStatus::kFloat:
      universal_gnss::SetOptionalValue(
          state,
          universal_gnss::GnssCapability::kRtkMode,
          state.rtk_mode,
          universal_gnss::GnssRtkMode::kFloat);
      break;
    case UbxCarrierSolutionStatus::kFixed:
      universal_gnss::SetOptionalValue(
          state,
          universal_gnss::GnssCapability::kRtkMode,
          state.rtk_mode,
          universal_gnss::GnssRtkMode::kFixed);
      break;
    case UbxCarrierSolutionStatus::kNone:
    default:
      universal_gnss::SetOptionalValue(
          state,
          universal_gnss::GnssCapability::kRtkMode,
          state.rtk_mode,
          universal_gnss::GnssRtkMode::kNone);
      break;
  }

  universal_gnss::SetOptionalValue(state,
                                   universal_gnss::GnssCapability::kSatellitesUsed,
                                   state.satellites_used,
                                   record.num_sv);
  SetCorrectionState(state, record.differential_solution);

  if (record.valid_date)
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kUtcDate,
                                     state.utc_date,
                                     universal_gnss::GnssUtcDate{record.year, record.month, record.day});
  }
  else
  {
    universal_gnss::ClearOptionalValue(
        state, universal_gnss::GnssCapability::kUtcDate, state.utc_date);
  }
  if (record.valid_time)
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kUtcTime,
                                     state.utc_time,
                                     universal_gnss::GnssUtcTime{
                                         record.hour, record.minute, record.second, record.nano_ns});
  }
  else
  {
    universal_gnss::ClearOptionalValue(
        state, universal_gnss::GnssCapability::kUtcTime, state.utc_time);
  }

  if (position_valid)
  {
    state.latitude_deg = record.latitude_deg;
    state.longitude_deg = record.longitude_deg;
    state.altitude_m = record.height_msl_m;

    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kHorizontalAccuracy,
                                     state.horizontal_accuracy_m,
                                     record.horizontal_accuracy_m);
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kVerticalAccuracy,
                                     state.vertical_accuracy_m,
                                     record.vertical_accuracy_m);
  }
  else
  {
    ClearPositionSolutionValues(state);
  }

  if (position_valid && record.ground_speed_mm_s >= 0)
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kSpeedOverGround,
                                     state.speed_over_ground_m_s,
                                     static_cast<float>(record.ground_speed_mm_s) / 1000.0f);
  }
  else
  {
    universal_gnss::ClearOptionalValue(state,
                                       universal_gnss::GnssCapability::kSpeedOverGround,
                                       state.speed_over_ground_m_s);
  }
  if (position_valid && record.course_over_ground_deg >= 0.0f &&
      record.course_over_ground_deg < 360.0f)
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kCourseOverGround,
                                     state.course_over_ground_deg,
                                     record.course_over_ground_deg);
  }
  else
  {
    universal_gnss::ClearOptionalValue(state,
                                       universal_gnss::GnssCapability::kCourseOverGround,
                                       state.course_over_ground_deg);
  }

  if (record.heading_vehicle_valid && position_valid)
  {
    universal_gnss::SetOptionalValue(
        state, universal_gnss::GnssCapability::kHeading, state.heading_deg, record.heading_vehicle_deg);
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kHeadingAccuracy,
                                     state.heading_accuracy_deg,
                                     record.heading_accuracy_deg);
  }
  else if (position_valid)
  {
    universal_gnss::ClearOptionalValue(
        state, universal_gnss::GnssCapability::kHeading, state.heading_deg);
    universal_gnss::ClearOptionalValue(state,
                                       universal_gnss::GnssCapability::kHeadingAccuracy,
                                       state.heading_accuracy_deg);
  }

  return state;
}

universal_gnss::GnssRuntimeState UbxNavDopToRuntimeState(const UbxNavDopRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.timestamp_ns;

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHdop);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kVdop);
  universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kHdop, state.hdop, record.h_dop);
  universal_gnss::SetOptionalValue(
      state, universal_gnss::GnssCapability::kVdop, state.vdop, record.v_dop);

  return state;
}

universal_gnss::GnssRuntimeState UbxNavSatToRuntimeState(const UbxNavSatRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.timestamp_ns;

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesVisible);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesUsed);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kMeanCn0);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kMaxCn0);

  universal_gnss::SetOptionalValue(state,
                                   universal_gnss::GnssCapability::kSatellitesVisible,
                                   state.satellites_visible,
                                   static_cast<std::uint16_t>(record.num_svs));
  universal_gnss::SetOptionalValue(state,
                                   universal_gnss::GnssCapability::kSatellitesUsed,
                                   state.satellites_used,
                                   static_cast<std::uint16_t>(record.used_satellite_count));

  std::uint32_t cn0_sum = 0u;
  std::uint32_t cn0_count = 0u;
  std::uint8_t max_cn0 = 0u;
  for (std::size_t index = 0; index < record.satellite_count; ++index)
  {
    const std::uint8_t cno = record.satellites[index].cno_db_hz;
    if (cno == 0u)
    {
      continue;
    }

    cn0_sum += cno;
    max_cn0 = (cn0_count == 0u || cno > max_cn0) ? cno : max_cn0;
    ++cn0_count;
  }

  if (cn0_count > 0u)
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kMeanCn0,
                                     state.mean_cn0_db_hz,
                                     static_cast<float>(cn0_sum) / static_cast<float>(cn0_count));
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kMaxCn0,
                                     state.max_cn0_db_hz,
                                     static_cast<float>(max_cn0));
  }

  return state;
}

universal_gnss::GnssRuntimeState UbxMonHwToRuntimeState(const UbxMonHwRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.timestamp_ns;

  if (record.layout != UbxMonHwLayout::kClassic)
  {
    return state;
  }

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kInterferenceState);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kJammingState);

  if (record.jamming_state == UbxMonRfJammingState::kUnknown)
  {
    return state;
  }

  const bool issue_detected = record.jamming_state == UbxMonRfJammingState::kWarning ||
                              record.jamming_state == UbxMonRfJammingState::kCritical;
  universal_gnss::SetOptionalValue(state,
                                   universal_gnss::GnssCapability::kInterferenceState,
                                   state.interference_detected,
                                   issue_detected);
  universal_gnss::SetOptionalValue(state,
                                   universal_gnss::GnssCapability::kJammingState,
                                   state.jamming_detected,
                                   issue_detected);
  return state;
}

universal_gnss::GnssRuntimeState UbxMonRfToRuntimeState(const UbxMonRfRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.timestamp_ns;

  if (record.block_count == 0u)
  {
    return state;
  }

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kInterferenceState);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kJammingState);

  bool has_known_monitor_state = false;
  bool issue_detected = false;
  for (std::size_t index = 0; index < static_cast<std::size_t>(record.block_count); ++index)
  {
    const UbxMonRfJammingState jamming_state = record.blocks[index].jamming_state;
    if (jamming_state == UbxMonRfJammingState::kUnknown)
    {
      continue;
    }

    has_known_monitor_state = true;
    if (jamming_state == UbxMonRfJammingState::kWarning ||
        jamming_state == UbxMonRfJammingState::kCritical)
    {
      issue_detected = true;
      break;
    }
  }

  if (has_known_monitor_state)
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kInterferenceState,
                                     state.interference_detected,
                                     issue_detected);
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kJammingState,
                                     state.jamming_detected,
                                     issue_detected);
  }

  return state;
}

}  // namespace universal_gnss_protocols
