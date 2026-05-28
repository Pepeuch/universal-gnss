#include "universal_gnss_protocols/ubx_parser.hpp"

#include <cstddef>
#include <cstdint>

namespace universal_gnss_protocols
{

namespace
{

constexpr std::uint8_t kUbxNavClass = 0x01u;
constexpr std::uint8_t kUbxMonClass = 0x0Au;
constexpr std::uint8_t kUbxNavStatusId = 0x03u;
constexpr std::uint8_t kUbxNavPvtId = 0x07u;
constexpr std::uint8_t kUbxNavSatId = 0x35u;
constexpr std::uint8_t kUbxMonRfId = 0x38u;
constexpr std::size_t kUbxNavStatusPayloadSize = 16u;
constexpr std::size_t kUbxNavPvtPayloadSize = 92u;
constexpr std::size_t kUbxNavSatHeaderSize = 8u;
constexpr std::size_t kUbxNavSatBlockSize = 12u;
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

}  // namespace

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
  record.heading_motion_deg = ScaleHeading1e5ToDegrees(ReadLeI4(frame.payload, 64u));
  record.heading_accuracy_deg = static_cast<float>(ReadLeU4(frame.payload, 72u)) * 1e-5f;
  record.heading_vehicle_deg = ScaleHeading1e5ToDegrees(ReadLeI4(frame.payload, 84u));

  return ParserResult<UbxNavPvtRecord>::RecordReady(std::move(record));
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

  if (record.heading_vehicle_valid && position_valid)
  {
    universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHeading);
    universal_gnss::SetOptionalValue(
        state, universal_gnss::GnssCapability::kHeading, state.heading_deg, record.heading_vehicle_deg);
  }

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
