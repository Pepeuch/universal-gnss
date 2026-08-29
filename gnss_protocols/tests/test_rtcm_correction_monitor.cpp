#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"
#include "universal_gnss_protocols/rtcm_records.hpp"

namespace
{

using universal_gnss::GnssDiagnosticSeverity;
using universal_gnss::GnssHealthSummary;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::RtcmConstellation;
using universal_gnss_protocols::RtcmCorrectionHealthOptions;
using universal_gnss_protocols::RtcmCorrectionMonitor;
using universal_gnss_protocols::RtcmFrame;
using universal_gnss_protocols::RtcmFrameFramer;
using universal_gnss_protocols::RtcmMessageInfo;
using universal_gnss_protocols::ParserStatus;

struct TestContext
{
  int failures{0};

  void Expect(bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }

  void ExpectNear(double lhs, double rhs, double epsilon, const std::string& message)
  {
    if (std::fabs(lhs - rhs) > epsilon)
    {
      ++failures;
      std::cerr << "FAILED: " << message << " lhs=" << lhs << " rhs=" << rhs << '\n';
    }
  }
};

std::vector<std::uint8_t> MakeRtcmPayload(const std::uint16_t message_type)
{
  return {
      static_cast<std::uint8_t>((message_type >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((message_type & 0x0Fu) << 4u),
  };
}

std::vector<std::uint8_t> BuildRtcmFrameBytes(const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> bytes = {
      0xD3u,
      static_cast<std::uint8_t>((payload.size() >> 8u) & 0x03u),
      static_cast<std::uint8_t>(payload.size() & 0xFFu),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  const std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

void AppendBit(std::vector<std::uint8_t>& payload, std::size_t& bit_offset, const bool bit)
{
  if ((bit_offset % 8u) == 0u)
  {
    payload.push_back(0u);
  }

  if (bit)
  {
    payload.back() |= static_cast<std::uint8_t>(1u << (7u - (bit_offset % 8u)));
  }
  ++bit_offset;
}

void AppendUnsignedBits(std::vector<std::uint8_t>& payload,
                        std::size_t& bit_offset,
                        const std::uint64_t value,
                        const std::size_t bit_count)
{
  for (std::size_t i = 0u; i < bit_count; ++i)
  {
    const std::size_t shift = bit_count - 1u - i;
    AppendBit(payload, bit_offset, ((value >> shift) & 0x01u) != 0u);
  }
}

void AppendSignedBits(std::vector<std::uint8_t>& payload,
                      std::size_t& bit_offset,
                      const std::int64_t value,
                      const std::size_t bit_count)
{
  const std::uint64_t mask = (1ULL << bit_count) - 1ULL;
  AppendUnsignedBits(payload, bit_offset, static_cast<std::uint64_t>(value) & mask, bit_count);
}

std::vector<std::uint8_t> BuildRtcm1230Payload(const std::uint16_t station_id,
                                               const bool code_phase_bias_indicator,
                                               const bool has_l1_ca_bias,
                                               const bool has_l1_p_bias,
                                               const bool has_l2_ca_bias,
                                               const bool has_l2_p_bias,
                                               const std::optional<std::int16_t> l1_ca_bias_raw,
                                               const std::optional<std::int16_t> l1_p_bias_raw,
                                               const std::optional<std::int16_t> l2_ca_bias_raw,
                                               const std::optional<std::int16_t> l2_p_bias_raw)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;
  AppendUnsignedBits(payload, bit_offset, 1230u, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, code_phase_bias_indicator ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 3u);
  AppendUnsignedBits(payload, bit_offset, has_l1_ca_bias ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, has_l1_p_bias ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, has_l2_ca_bias ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, has_l2_p_bias ? 1u : 0u, 1u);
  if (has_l1_ca_bias)
  {
    AppendSignedBits(payload, bit_offset, *l1_ca_bias_raw, 16u);
  }
  if (has_l1_p_bias)
  {
    AppendSignedBits(payload, bit_offset, *l1_p_bias_raw, 16u);
  }
  if (has_l2_ca_bias)
  {
    AppendSignedBits(payload, bit_offset, *l2_ca_bias_raw, 16u);
  }
  if (has_l2_p_bias)
  {
    AppendSignedBits(payload, bit_offset, *l2_p_bias_raw, 16u);
  }
  return payload;
}

std::vector<std::uint8_t> CapturedRtcm1006FrameBytes()
{
  return {
      0xD3u, 0x00u, 0x15u, 0x3Eu, 0xE0u, 0x01u, 0x03u, 0x0Au, 0xB3u, 0x4Bu,
      0x6Eu, 0x4Au, 0x80u, 0x69u, 0x58u, 0x11u, 0xB8u, 0x0Au, 0x41u, 0x56u,
      0xB9u, 0xA1u, 0x00u, 0x00u, 0xE1u, 0x25u, 0x6Du,
  };
}

void AppendZeroBits(std::vector<std::uint8_t>& payload,
                    std::size_t& bit_offset,
                    const std::size_t bit_count)
{
  for (std::size_t index = 0u; index < bit_count; ++index)
  {
    AppendBit(payload, bit_offset, false);
  }
}

std::size_t GetRtcmMsmBodyBits(const std::uint8_t msm_variant,
                               const std::size_t satellite_count,
                               const std::size_t populated_cell_count)
{
  switch (msm_variant)
  {
    case 1u:
      return satellite_count * 10u + populated_cell_count * 15u;
    case 2u:
      return satellite_count * 10u + populated_cell_count * 27u;
    case 3u:
      return satellite_count * 10u + populated_cell_count * 42u;
    case 4u:
      return satellite_count * 18u + populated_cell_count * 48u;
    case 5u:
      return satellite_count * 36u + populated_cell_count * 63u;
    case 6u:
      return satellite_count * 18u + populated_cell_count * 65u;
    case 7u:
      return satellite_count * 36u + populated_cell_count * 80u;
    default:
      return 0u;
  }
}

std::vector<std::uint8_t> BuildRtcmMsmPayload(const std::uint16_t message_type,
                                              const std::uint16_t station_id,
                                              const std::vector<std::uint8_t>& satellite_ids,
                                              const std::vector<std::uint8_t>& signal_ids,
                                              const std::vector<bool>& cell_mask,
                                              const bool multiple_message = false,
                                              const std::uint8_t issue_of_data_station = 0u)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;

  AppendUnsignedBits(payload, bit_offset, message_type, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, 123456u, 30u);
  AppendUnsignedBits(payload, bit_offset, multiple_message ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, issue_of_data_station, 3u);
  AppendUnsignedBits(payload, bit_offset, 15u, 7u);
  AppendUnsignedBits(payload, bit_offset, 1u, 2u);
  AppendUnsignedBits(payload, bit_offset, 0u, 2u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 3u, 3u);

  for (std::uint8_t satellite = 1u; satellite <= 64u; ++satellite)
  {
    bool present = false;
    for (const auto candidate : satellite_ids)
    {
      if (candidate == satellite)
      {
        present = true;
        break;
      }
    }
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
  }

  for (std::uint8_t signal = 1u; signal <= 32u; ++signal)
  {
    bool present = false;
    for (const auto candidate : signal_ids)
    {
      if (candidate == signal)
      {
        present = true;
        break;
      }
    }
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
  }

  std::size_t populated_cell_count = 0u;
  for (const bool present : cell_mask)
  {
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
    if (present)
    {
      ++populated_cell_count;
    }
  }

  AppendZeroBits(payload,
                 bit_offset,
                 GetRtcmMsmBodyBits(universal_gnss_protocols::GetRtcmMsmVariant(message_type),
                                    satellite_ids.size(),
                                    populated_cell_count));
  return payload;
}

std::vector<std::uint8_t> BuildRtcm1005Payload(const std::uint16_t station_id)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;
  AppendUnsignedBits(payload, bit_offset, 1005u, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, 0u, 6u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendSignedBits(payload, bit_offset, 0, 38u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendSignedBits(payload, bit_offset, 0, 38u);
  AppendUnsignedBits(payload, bit_offset, 0u, 2u);
  AppendSignedBits(payload, bit_offset, 0, 38u);
  return payload;
}

std::vector<std::uint8_t> BuildRtcm1006Payload(const std::uint16_t station_id)
{
  std::vector<std::uint8_t> payload = BuildRtcm1005Payload(station_id);
  payload[0] = static_cast<std::uint8_t>((1006u >> 4u) & 0xFFu);
  payload[1] = static_cast<std::uint8_t>((payload[1] & 0x0Fu) | ((1006u & 0x0Fu) << 4u));
  std::size_t bit_offset = 152u;
  AppendUnsignedBits(payload, bit_offset, 25u, 16u);
  return payload;
}

RtcmFrame MakeDecodedBaseFrame(const std::uint16_t station_id,
                               const std::int64_t timestamp_ns)
{
  RtcmFrame frame;
  frame.timestamp_ns = timestamp_ns;
  frame.payload = BuildRtcm1005Payload(station_id);
  frame.checksum_status = ChecksumStatus::kValid;
  return frame;
}

RtcmFrame MakeDecodedMsmFrame(const std::uint16_t station_id,
                              const std::int64_t timestamp_ns)
{
  RtcmFrame frame;
  frame.timestamp_ns = timestamp_ns;
  frame.payload = BuildRtcmMsmPayload(1077u, station_id, {1u}, {1u}, {true});
  frame.checksum_status = ChecksumStatus::kValid;
  return frame;
}

RtcmFrame MakeValidRtcmFrame(const std::uint16_t message_type,
                             const std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  RtcmFrame frame;
  frame.timestamp_ns = timestamp_ns;
  frame.payload = MakeRtcmPayload(message_type);
  frame.checksum_status = ChecksumStatus::kValid;
  return frame;
}

RtcmFrame ParseSingleFrame(const std::vector<std::uint8_t>& bytes)
{
  RtcmFrameFramer framer;
  for (const auto byte : bytes)
  {
    auto result = framer.PushByte(byte);
    if (result.status == ParserStatus::kRecordReady && result.record.has_value())
    {
      return *result.record;
    }
  }

  const auto finalize = framer.Finalize();
  if (finalize.status == ParserStatus::kRecordReady && finalize.record.has_value())
  {
    return *finalize.record;
  }

  return RtcmFrame{};
}

RtcmFrame MakeIntegrityValidFrame(const std::vector<std::uint8_t>& payload,
                                  const std::int64_t timestamp_ns)
{
  RtcmFrame frame = ParseSingleFrame(BuildRtcmFrameBytes(payload));
  frame.timestamp_ns = timestamp_ns;
  return frame;
}

RtcmMessageInfo MakeMessageInfo(const std::uint16_t message_type)
{
  RtcmMessageInfo info;
  info.message_type = message_type;
  info.is_station_arp = message_type == 1005u || message_type == 1006u;
  info.is_glonass_bias = message_type == 1230u;
  info.msm_variant = universal_gnss_protocols::GetRtcmMsmVariant(message_type);
  switch (message_type)
  {
    case 1074u:
    case 1077u:
      info.is_msm = true;
      info.msm_constellation = RtcmConstellation::kGps;
      break;
    case 1087u:
      info.is_msm = true;
      info.msm_constellation = RtcmConstellation::kGlonass;
      break;
    case 1097u:
      info.is_msm = true;
      info.msm_constellation = RtcmConstellation::kGalileo;
      break;
    case 1127u:
      info.is_msm = true;
      info.msm_constellation = RtcmConstellation::kBeiDou;
      break;
    default:
      break;
  }
  return info;
}

const universal_gnss_protocols::RtcmSemanticObservation* FindObservation(
    const universal_gnss_protocols::RtcmSemanticObservations& observations,
    const std::string& name)
{
  for (const auto& observation : observations)
  {
    if (observation.name == name)
    {
      return &observation;
    }
  }
  return nullptr;
}

void TestMessageCountsAndLastSeen(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1005u), 100);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 200);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 350);

  ctx.Expect(monitor.total_frames() == 3u, "valid observations should increment total frame count");
  ctx.Expect(monitor.valid_frames() == 3u, "valid observations should increment valid frame count");
  ctx.Expect(monitor.invalid_frames() == 0u, "valid observations should not increment invalid count");
  ctx.Expect(monitor.last_frame_timestamp_ns() == std::optional<std::int64_t>(350),
             "monitor should retain the latest frame timestamp");
  ctx.Expect(monitor.MessageCount(1005u) == 1u, "message type 1005 should be counted once");
  ctx.Expect(monitor.MessageCount(1077u) == 2u, "message type 1077 should be counted twice");
  ctx.Expect(monitor.LastSeenMessageTimestampNs(1077u) == std::optional<std::int64_t>(350),
             "last-seen timestamp should track the latest 1077 message");
  ctx.Expect(monitor.AgeSinceMessageTypeNs(1077u, 500) == std::optional<std::int64_t>(150),
             "age helper should subtract the latest message timestamp from now");
  ctx.Expect(monitor.HasRequiredMessageTypes({1005u, 1077u}),
             "required-message helper should succeed when all message types were observed");
}

void TestRateHelpers(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1077u), 1000000000LL);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 2000000000LL);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 6000000000LL);
  monitor.ObserveInvalidFrame(6500000000LL);

  const auto message_rate_hz = monitor.MessageRateHz(1077u, 6000000000LL, 4000000000LL);
  ctx.Expect(message_rate_hz.has_value(), "timestamped message observations should produce a rate");
  if (message_rate_hz.has_value())
  {
    ctx.ExpectNear(*message_rate_hz, 0.5, 1e-9, "1077 rate should count only timestamps in window");
  }

  const auto total_frame_rate_hz = monitor.TotalFrameRateHz(6500000000LL, 1000000000LL);
  ctx.Expect(total_frame_rate_hz.has_value(), "timestamped frames should produce a total rate");
  if (total_frame_rate_hz.has_value())
  {
    ctx.ExpectNear(*total_frame_rate_hz, 2.0, 1e-9, "frame-rate helper should include valid and invalid frames");
  }
}

void TestTimestampHistoryRetention(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  constexpr std::size_t kObservationCount = 100000u;
  constexpr std::int64_t kTimestampStepNs = 1000000LL;
  for (std::size_t index = 0u; index < kObservationCount; ++index)
  {
    const std::int64_t timestamp_ns = static_cast<std::int64_t>(index) * kTimestampStepNs;
    if ((index % 3u) == 0u)
    {
      monitor.ObserveMessage(MakeMessageInfo(1077u), timestamp_ns);
    }
    else if ((index % 3u) == 1u)
    {
      monitor.ObserveMessage(MakeMessageInfo(1087u), timestamp_ns);
    }
    else
    {
      monitor.ObserveInvalidFrame(timestamp_ns);
    }
  }

  const std::int64_t now_timestamp_ns =
      static_cast<std::int64_t>(kObservationCount - 1u) * kTimestampStepNs;
  const auto total_rate_hz = monitor.TotalFrameRateHz(now_timestamp_ns, 1000000000LL);
  const auto gps_rate_hz =
      monitor.MsmConstellationRateHz(RtcmConstellation::kGps, now_timestamp_ns, 1000000000LL);
  const auto expired_rate_hz = monitor.MessageRateHz(1077u, 1000000000LL, 1000000000LL);
  ctx.Expect(total_rate_hz.has_value() && *total_rate_hz > 999.0,
             "recent total-frame rate should remain correct after sustained RTCM input");
  ctx.Expect(gps_rate_hz.has_value() && *gps_rate_hz > 330.0,
             "recent constellation rate should remain correct after sustained mixed MSM input");
  ctx.Expect(expired_rate_hz.has_value() && *expired_rate_hz == 0.0,
             "timestamped observations outside the retained diagnostic horizon must expire");
  ctx.Expect(monitor.total_frames() == kObservationCount &&
                 monitor.valid_frames() + monitor.invalid_frames() == kObservationCount,
             "lifetime frame counters must remain correct after history retention");
  ctx.Expect(monitor.RetainedTimestampCount() < 250000u,
             "timestamp histories must remain bounded during sustained mixed RTCM input");
}

void TestMsmConstellationTracking(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1074u), 100000000LL);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 200000000LL);
  monitor.ObserveMessage(MakeMessageInfo(1087u), 250000000LL);

  ctx.Expect(monitor.HasSeenAnyMsmMessage(), "MSM observation should be tracked");
  ctx.Expect(monitor.MsmConstellationCount(RtcmConstellation::kGps) == 2u,
             "GPS MSM count should aggregate multiple GPS MSM messages");
  ctx.Expect(monitor.MsmConstellationCount(RtcmConstellation::kGlonass) == 1u,
             "GLONASS MSM count should reflect observed GLONASS MSM messages");
  ctx.Expect(monitor.LastSeenMsmConstellationTimestampNs(RtcmConstellation::kGps) ==
                 std::optional<std::int64_t>(200000000LL),
             "last-seen timestamp should be retained per MSM constellation");
  ctx.Expect(monitor.AgeSinceMsmConstellationNs(RtcmConstellation::kGlonass, 400000000LL) ==
                 std::optional<std::int64_t>(150000000LL),
             "MSM age helper should use the latest constellation timestamp");

  const auto gps_rate_hz =
      monitor.MsmConstellationRateHz(RtcmConstellation::kGps, 200000000LL, 200000000LL);
  ctx.Expect(gps_rate_hz.has_value(), "timestamped MSM constellations should produce a rate");
  if (gps_rate_hz.has_value())
  {
    ctx.ExpectNear(*gps_rate_hz, 10.0, 1e-9, "MSM constellation rate should be windowed");
  }
}

void TestBasePositionAndGlonassBiasTracking(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1005u), 100);
  monitor.ObserveMessage(MakeMessageInfo(1006u), 200);
  monitor.ObserveMessage(MakeMessageInfo(1230u), 300);

  ctx.Expect(monitor.HasSeenBasePositionMessage(),
             "station ARP observations should mark base-position availability");
  ctx.Expect(monitor.HasSeenBasePosition1005(), "1005 should be tracked individually");
  ctx.Expect(monitor.HasSeenBasePosition1006(), "1006 should be tracked individually");
  ctx.Expect(monitor.HasSeenGlonassBias1230(), "1230 should be tracked as seen");
  RtcmCorrectionHealthOptions options;
  options.required_message_types = {1230u};
  options.require_base_position = true;
  options.require_glonass_bias = true;
  ctx.Expect(monitor.HasRequiredCorrectionMessages(options),
             "required-corrections helper should reflect base and 1230 observations");
}

void TestInvalidFrameHandling(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveFrame(MakeValidRtcmFrame(1077u, 100));

  RtcmFrame invalid_checksum = MakeValidRtcmFrame(1077u, 200);
  invalid_checksum.checksum_status = ChecksumStatus::kInvalid;
  monitor.ObserveFrame(invalid_checksum);

  RtcmFrame truncated_payload = MakeValidRtcmFrame(1087u, 300);
  truncated_payload.payload = {0x43u};
  monitor.ObserveFrame(truncated_payload);

  ctx.Expect(monitor.total_frames() == 3u, "all frame observations should increment total count");
  ctx.Expect(monitor.valid_frames() == 1u, "only checksum-valid, parseable frames should be valid");
  ctx.Expect(monitor.invalid_frames() == 2u, "invalid frames should be counted");
  ctx.Expect(monitor.MessageCount(1077u) == 1u, "invalid frames should not populate message counts");
  ctx.Expect(monitor.last_frame_timestamp_ns() == std::optional<std::int64_t>(300),
             "latest timestamp should include invalid frames");
}

void TestGlonassBiasDecodeTracking(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;

  RtcmFrame valid_1230 = MakeValidRtcmFrame(1230u, 1000);
  valid_1230.payload = BuildRtcm1230Payload(42u,
                                            true,
                                            true,
                                            false,
                                            true,
                                            false,
                                            10,
                                            std::nullopt,
                                            -5,
                                            std::nullopt);
  monitor.ObserveFrame(valid_1230);

  RtcmFrame malformed_1230 = MakeValidRtcmFrame(1230u, 1500);
  malformed_1230.payload = BuildRtcm1230Payload(42u,
                                                true,
                                                true,
                                                true,
                                                false,
                                                false,
                                                10,
                                                12,
                                                std::nullopt,
                                                std::nullopt);
  malformed_1230.payload.pop_back();
  monitor.ObserveFrame(malformed_1230);

  ctx.Expect(monitor.HasSeenGlonassBias1230(),
             "RTCM 1230 frames should mark GLONASS bias presence");
  ctx.Expect(monitor.HasDecodedGlonassBias1230() && monitor.LastGlonassBias1230Valid(),
             "successfully decoded RTCM 1230 content should be retained as valid");
  ctx.Expect(monitor.LastGlonassBias1230TimestampNs() == std::optional<std::int64_t>(1500),
             "last-seen RTCM 1230 timestamp should include malformed payloads");
  ctx.Expect(monitor.LastDecodedGlonassBias1230TimestampNs() == std::optional<std::int64_t>(1000),
             "last-decoded RTCM 1230 timestamp should reflect the latest semantic decode success");
  ctx.Expect(monitor.GlonassBias1230DecodeSuccessCount() == 1u &&
                 monitor.GlonassBias1230DecodeFailureCount() == 1u &&
                 monitor.GlonassBias1230MalformedCount() == 1u,
             "RTCM 1230 decode success/failure counters should distinguish malformed payloads");
  ctx.Expect(monitor.AgeSinceGlonassBias1230Ns(2000) == std::optional<std::int64_t>(500),
             "RTCM 1230 age should be based on the last observed 1230 frame");
  if (monitor.last_glonass_code_phase_bias().has_value())
  {
    ctx.Expect(monitor.last_glonass_code_phase_bias()->station_id == 42u &&
                   monitor.last_glonass_code_phase_bias()->signal_mask == 0x05u,
               "decoded RTCM 1230 content should expose station id and signal mask");
  }
}

void TestBaseStationArpSemanticObservationFromCaptured1006(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  RtcmFrame frame = ParseSingleFrame(CapturedRtcm1006FrameBytes());
  frame.timestamp_ns = 1000;
  monitor.ObserveFrame(frame);

  const auto observations = universal_gnss_protocols::BuildRtcmSemanticObservations(monitor, 1500);
  const auto* base_station_arp = FindObservation(observations, "base_station_arp");

  ctx.Expect(base_station_arp != nullptr &&
                 base_station_arp->message_type == 1006u &&
                 base_station_arp->seen &&
                 base_station_arp->decoded &&
                 base_station_arp->valid &&
                 base_station_arp->decode_success_count == 1u &&
                 base_station_arp->decode_failure_count == 0u &&
                 base_station_arp->malformed_count == 0u &&
                 base_station_arp->age_ns == std::optional<std::int64_t>(500),
             "captured RTCM 1006 should produce a decoded and valid base-station semantic observation");
  ctx.Expect(monitor.last_base_station_arp().has_value() &&
                 monitor.last_base_station_arp()->message_type == 1006u &&
                 monitor.last_base_station_arp()->station_id == 1u &&
                 monitor.last_base_station_arp()->antenna_height_m.has_value(),
             "captured RTCM 1006 should populate the correction monitor base-station record");
}

void TestMsmDecodeTracking(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;

  RtcmFrame gps_msm7 = MakeValidRtcmFrame(1077u, 1000);
  gps_msm7.payload = BuildRtcmMsmPayload(1077u,
                                         42u,
                                         {1u, 3u},
                                         {1u, 5u},
                                         {true, false, true, true},
                                         true,
                                         5u);
  monitor.ObserveFrame(gps_msm7);

  RtcmFrame glonass_msm7 = MakeValidRtcmFrame(1087u, 1500);
  glonass_msm7.payload = BuildRtcmMsmPayload(1087u,
                                             42u,
                                             {2u},
                                             {1u, 3u, 4u},
                                             {true, false, true});
  monitor.ObserveFrame(glonass_msm7);

  RtcmFrame malformed_msm7 = MakeValidRtcmFrame(1077u, 1800);
  malformed_msm7.payload = BuildRtcmMsmPayload(1077u,
                                               42u,
                                               {1u},
                                               {1u},
                                               {true});
  malformed_msm7.payload.resize(21u);
  monitor.ObserveFrame(malformed_msm7);

  ctx.Expect(monitor.HasSeenAnyMsmMessage(),
             "decoded MSM frames should mark generic MSM presence");
  ctx.Expect(monitor.HasDecodedAnyMsmSummary(),
             "decoded MSM frames should retain the latest semantic summary");
  ctx.Expect(monitor.LastMsmTimestampNs() == std::optional<std::int64_t>(1800),
             "latest MSM timestamp should include malformed trailing frames");
  ctx.Expect(monitor.LastDecodedMsmTimestampNs() == std::optional<std::int64_t>(1500),
             "latest decoded MSM timestamp should retain the latest semantic decode success");
  ctx.Expect(monitor.MsmDecodeSuccessCount() == 2u &&
                 monitor.MsmDecodeFailureCount() == 1u &&
                 monitor.MsmMalformedCount() == 1u,
             "MSM decode counters should distinguish successful and malformed payloads");
  if (monitor.last_msm_summary().has_value())
  {
    ctx.Expect(monitor.last_msm_summary()->message_type == 1087u &&
                   monitor.last_msm_summary()->station_id == 42u &&
                   monitor.last_msm_summary()->constellation == RtcmConstellation::kGlonass &&
                   monitor.last_msm_summary()->satellite_count == 1u &&
                   monitor.last_msm_summary()->signal_count == 3u &&
                   monitor.last_msm_summary()->cell_count == 2u,
               "the latest decoded MSM summary should expose station, constellation, and counts");
  }

  const auto gps_stats = monitor.msm_summary_activity().find(1077u);
  const auto glonass_stats = monitor.msm_summary_activity().find(1087u);
  ctx.Expect(gps_stats != monitor.msm_summary_activity().end() &&
                 gps_stats->second.decode_success_count == 1u &&
                 gps_stats->second.decode_failure_count == 1u &&
                 gps_stats->second.malformed_count == 1u,
             "per-message MSM stats should retain GPS decode and malformed counters");
  ctx.Expect(glonass_stats != monitor.msm_summary_activity().end() &&
                 glonass_stats->second.decode_success_count == 1u &&
                 glonass_stats->second.decode_failure_count == 0u &&
                 glonass_stats->second.last_summary.has_value() &&
                 glonass_stats->second.last_summary->cell_count == 2u,
             "per-message MSM stats should retain GLONASS decode results");
}

void TestMsmSemanticObservations(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;

  RtcmFrame gps_msm7 = MakeValidRtcmFrame(1077u, 1000);
  gps_msm7.payload = BuildRtcmMsmPayload(1077u,
                                         42u,
                                         {1u, 3u},
                                         {1u, 5u},
                                         {true, false, true, true},
                                         true,
                                         5u);
  monitor.ObserveFrame(gps_msm7);

  RtcmFrame glonass_msm7 = MakeValidRtcmFrame(1087u, 1500);
  glonass_msm7.payload = BuildRtcmMsmPayload(1087u,
                                             42u,
                                             {2u},
                                             {1u, 3u, 4u},
                                             {true, false, true});
  monitor.ObserveFrame(glonass_msm7);

  const auto observations = universal_gnss_protocols::BuildRtcmSemanticObservations(monitor, 2000);
  const auto* summary = FindObservation(observations, "msm_summary");
  const auto* gps = FindObservation(observations, "msm_gps_msm7");
  const auto* glonass = FindObservation(observations, "msm_glonass_msm7");

  ctx.Expect(summary != nullptr && summary->seen && summary->decoded && summary->valid &&
                 summary->message_type == 1087u &&
                 summary->age_ns == std::optional<std::int64_t>(500) &&
                 summary->decode_success_count == 2u &&
                 summary->decode_failure_count == 0u,
             "the aggregate MSM semantic observation should expose the latest summary state");
  if (summary != nullptr)
  {
    bool saw_station = false;
    bool saw_constellations = false;
    for (const auto& field : summary->fields)
    {
      if (field.key == "station_id" && field.value == "42")
      {
        saw_station = true;
      }
      if (field.key == "constellations_seen" && field.value == "gps,glonass")
      {
        saw_constellations = true;
      }
    }
    ctx.Expect(saw_station && saw_constellations,
               "the aggregate MSM semantic observation should expose the latest station and seen constellations");
  }

  ctx.Expect(gps != nullptr && gps->message_type == 1077u && gps->decoded && gps->valid,
             "GPS MSM semantic observations should be emitted per message type");
  ctx.Expect(glonass != nullptr &&
                 glonass->message_type == 1087u &&
                 glonass->decoded &&
                 glonass->valid &&
                 glonass->last_decoded_timestamp_ns == std::optional<std::int64_t>(1500),
             "GLONASS MSM semantic observations should retain the latest decoded timestamp");
}

void TestMsmMalformedHealthEvent(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  RtcmFrame malformed_msm7 = MakeValidRtcmFrame(1077u, 1800);
  malformed_msm7.payload = BuildRtcmMsmPayload(1077u,
                                               42u,
                                               {1u},
                                               {1u},
                                               {true});
  malformed_msm7.payload.resize(21u);
  monitor.ObserveFrame(malformed_msm7);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 2000;
  options.stale_after_ns = 5000;
  options.require_any_msm = false;
  const GnssHealthSummary health = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      monitor,
      options);

  bool found_msm_malformed = false;
  for (const auto& event : health.events)
  {
    if (event.code == "rtcm.msm_malformed")
    {
      found_msm_malformed = true;
      break;
    }
  }
  ctx.Expect(found_msm_malformed,
             "RTCM health should surface malformed MSM payloads as parser diagnostics");
}

void TestHealthStates(TestContext& ctx)
{
  RtcmCorrectionMonitor healthy_monitor;
  healthy_monitor.ObserveMessage(MakeMessageInfo(1077u), 1000);

  RtcmCorrectionHealthOptions healthy_options;
  healthy_options.now_timestamp_ns = 1500;
  healthy_options.stale_after_ns = 1000;
  healthy_options.require_any_msm = true;
  const GnssHealthSummary healthy = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      healthy_monitor,
      healthy_options);
  ctx.Expect(healthy.overall_severity == GnssDiagnosticSeverity::kOk,
             "recent RTCM activity should yield ok health");
  ctx.Expect(healthy.correction_available, "recent required RTCM activity should be available");
  ctx.Expect(!healthy.stale_data, "recent RTCM activity should not be marked stale");

  RtcmCorrectionHealthOptions stale_options;
  stale_options.now_timestamp_ns = 3000;
  stale_options.stale_after_ns = 1000;
  stale_options.require_any_msm = true;
  const GnssHealthSummary stale = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      healthy_monitor,
      stale_options);
  ctx.Expect(stale.overall_severity == GnssDiagnosticSeverity::kWarning,
             "stale RTCM activity should yield a warning");
  ctx.Expect(stale.stale_data, "stale RTCM activity should set stale_data");
  ctx.Expect(!stale.correction_available, "stale RTCM activity should not report current availability");

  RtcmCorrectionMonitor unknown_monitor;
  unknown_monitor.ObserveMessage(MakeMessageInfo(1077u));
  RtcmCorrectionHealthOptions unknown_options;
  unknown_options.now_timestamp_ns = 5000;
  unknown_options.stale_after_ns = 1000;
  unknown_options.require_any_msm = true;
  const GnssHealthSummary unknown = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      unknown_monitor,
      unknown_options);
  ctx.Expect(unknown.overall_severity == GnssDiagnosticSeverity::kUnknown,
             "timestamp-less RTCM activity should yield unknown freshness");

  RtcmCorrectionMonitor missing_required_monitor;
  missing_required_monitor.ObserveMessage(MakeMessageInfo(1005u), 1000);
  RtcmCorrectionHealthOptions error_options;
  error_options.now_timestamp_ns = 1200;
  error_options.stale_after_ns = 1000;
  error_options.required_message_types = {1077u};
  error_options.require_base_position = true;
  const GnssHealthSummary error = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      missing_required_monitor,
      error_options);
  ctx.Expect(error.overall_severity == GnssDiagnosticSeverity::kError,
             "missing required correction content should yield an error");
  ctx.Expect(error.HasErrors(), "missing required correction content should emit an error event");
}

void TestFutureTimestampsCannotSatisfyFreshness(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1077u), 10000);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 1000;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 5000;
  options.startup_grace_ns = 5000;
  options.require_any_msm = true;

  const auto age_ns = monitor.AgeSinceLastFrameNs(*options.now_timestamp_ns);
  const GnssHealthSummary health =
      universal_gnss_protocols::BuildRtcmCorrectionHealth(monitor, options);
  ctx.Expect(!age_ns.has_value(),
             "an observation later than diagnostic now must not produce a negative age");
  ctx.Expect(!monitor.HasRequiredCorrectionMessages(options),
             "a future observation must not satisfy a bounded correction requirement");
  ctx.Expect(!health.correction_available,
             "a future observation must not make corrections available");

  bool found_freshness_unknown = false;
  for (const auto& event : health.events)
  {
    if (event.code == "rtcm.freshness_unknown")
    {
      found_freshness_unknown = true;
      break;
    }
  }
  ctx.Expect(found_freshness_unknown,
             "future-dated correction activity should report unknown freshness");
}

void TestPortableRtkRequirementsAccept1006(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1006u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 1100);
  monitor.ObserveMessage(MakeMessageInfo(1230u), 1200);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 2000;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 10000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  const GnssHealthSummary health = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      monitor,
      options);
  ctx.Expect(options.required_msm_constellations.empty() && options.require_any_msm,
             "portable RTK requirements should require recent MSM presence without pinning specific constellations");
  ctx.Expect(monitor.HasRequiredCorrectionMessages(options),
             "portable RTK requirements should accept 1006 as the base-position message");
  ctx.Expect(health.correction_available,
             "complete portable RTCM content should report correction availability");
  ctx.Expect(health.overall_severity == GnssDiagnosticSeverity::kOk,
             "portable RTCM content with one recent MSM constellation should clear the missing-message diagnostic");
}

void TestPortableRtkRequirementsUseRecentObservationWindow(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1005u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 9000);
  monitor.ObserveMessage(MakeMessageInfo(1087u), 9100);
  monitor.ObserveMessage(MakeMessageInfo(1097u), 9200);
  monitor.ObserveMessage(MakeMessageInfo(1127u), 9300);
  monitor.ObserveMessage(MakeMessageInfo(1230u), 9400);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 12000;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 2000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  const GnssHealthSummary health = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      monitor,
      options);
  ctx.Expect(!monitor.HasRequiredCorrectionMessages(options),
             "portable RTK requirements should expire base-position messages outside the recent window");
  ctx.Expect(health.overall_severity == GnssDiagnosticSeverity::kError,
             "missing recent required RTCM content should remain an error after startup grace");
}

void TestStaticBaseMetadataOutlivesDynamicObservationWindow(TestContext& ctx)
{
  constexpr std::int64_t kSecond = 1000000000LL;
  RtcmCorrectionMonitor monitor;
  monitor.ObserveFrame(MakeDecodedBaseFrame(42u, 0));
  monitor.ObserveFrame(MakeDecodedMsmFrame(42u, 30 * kSecond));
  monitor.ObserveFrame(MakeDecodedMsmFrame(42u, 31 * kSecond));

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 31 * kSecond;
  options.stale_after_ns = 5 * kSecond;
  options.required_observation_window_ns = 30 * kSecond;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  const auto health = universal_gnss_protocols::BuildRtcmCorrectionHealth(monitor, options);
  ctx.Expect(monitor.AgeSinceBaseStationArpNs(*options.now_timestamp_ns) ==
                 std::optional<std::int64_t>(31 * kSecond),
             "retained static base metadata should continue reporting its age");
  ctx.Expect(monitor.HasRequiredCorrectionMessages(options) && health.correction_available,
             "fresh MSM must remain healthy after valid 1005 metadata exceeds the dynamic window");

  options.now_timestamp_ns = 62 * kSecond;
  ctx.Expect(!monitor.HasRequiredCorrectionMessages(options),
             "retained static metadata must not keep expired dynamic MSM healthy");
}

void TestStationOwnershipPreventsMixedCorrectionHealth(TestContext& ctx)
{
  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 3000;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 5000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  RtcmCorrectionMonitor base_a_msm_b;
  base_a_msm_b.ObserveFrame(MakeDecodedBaseFrame(10u, 1000));
  base_a_msm_b.ObserveFrame(MakeDecodedMsmFrame(11u, 2000));
  ctx.Expect(!base_a_msm_b.HasRequiredCorrectionMessages(options),
             "station A base metadata must not combine with station B MSM");
  ctx.Expect(!base_a_msm_b.last_base_station_arp().has_value(),
             "a decoded station transition must invalidate incompatible retained base metadata");
  base_a_msm_b.ObserveFrame(MakeDecodedBaseFrame(11u, 2500));
  ctx.Expect(base_a_msm_b.HasRequiredCorrectionMessages(options) &&
                 base_a_msm_b.station_id() == std::optional<std::uint16_t>(11u),
             "valid replacement metadata for the current station should restore correction health");

  base_a_msm_b.ObserveMessage(MakeMessageInfo(1013u), 2700);
  ctx.Expect(base_a_msm_b.station_id() == std::optional<std::uint16_t>(11u) &&
                 base_a_msm_b.HasRequiredCorrectionMessages(options),
             "non-station-bearing messages must not fabricate or replace station ownership");

  RtcmCorrectionMonitor msm_a_base_b;
  msm_a_base_b.ObserveFrame(MakeDecodedMsmFrame(10u, 1000));
  msm_a_base_b.ObserveFrame(MakeDecodedBaseFrame(11u, 2000));
  ctx.Expect(!msm_a_base_b.HasRequiredCorrectionMessages(options),
             "station A MSM must not combine with station B base metadata");
  ctx.Expect(msm_a_base_b.MsmConstellationCount(RtcmConstellation::kGps) == 0u,
             "a decoded station transition must invalidate incompatible dynamic observations");

  RtcmCorrectionMonitor boundary_station_ids;
  boundary_station_ids.ObserveFrame(MakeDecodedBaseFrame(0u, 1000));
  boundary_station_ids.ObserveFrame(MakeDecodedMsmFrame(0u, 2000));
  ctx.Expect(boundary_station_ids.HasRequiredCorrectionMessages(options),
             "RTCM station id zero must be handled as a valid 12-bit identity");
  boundary_station_ids.ObserveFrame(MakeDecodedBaseFrame(4095u, 2500));
  ctx.Expect(!boundary_station_ids.HasRequiredCorrectionMessages(options),
             "the maximum station id must trigger the same deterministic ownership transition");
  boundary_station_ids.Reset();
  ctx.Expect(!boundary_station_ids.station_id().has_value() &&
                 !boundary_station_ids.HasSeenBasePositionMessage() &&
                 boundary_station_ids.valid_frames() == 0u,
             "a full monitor reset must deterministically clear ownership and retained state");
}

void TestPortableRtkRequirementsRespectStartupGrace(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1077u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1087u), 1100);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 2500;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 10000;
  options.startup_grace_ns = 5000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  const GnssHealthSummary health = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      monitor,
      options);
  ctx.Expect(health.overall_severity == GnssDiagnosticSeverity::kInfo,
             "startup grace should defer the missing required RTCM error while the stream is still collecting");
  ctx.Expect(!health.HasErrors(),
             "startup grace should avoid emitting a hard required-message error");
}

void TestPortableRtkRequirementsDoNotRequireGlonassBias(TestContext& ctx)
{
  // Base position + a recent MSM constellation, but NO RTCM 1230 at all — the
  // common case for public casters that never transmit GLONASS code-phase bias.
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1006u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 1100);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 2000;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 10000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  ctx.Expect(!options.require_glonass_bias,
             "portable RTK requirements must treat RTCM 1230 GLONASS bias as optional");

  const GnssHealthSummary health = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      monitor,
      options);
  ctx.Expect(monitor.HasRequiredCorrectionMessages(options),
             "base position plus a recent MSM should satisfy portable RTK requirements without 1230");
  ctx.Expect(health.correction_available,
             "correction availability must not depend on GLONASS 1230");
  ctx.Expect(health.overall_severity == GnssDiagnosticSeverity::kOk,
             "a stream without RTCM 1230 must not raise required_messages_missing");
}

void TestDecodedInvalid1230IsInformationalAndCorrectionStillAvailable(TestContext& ctx)
{
  // Base + MSM present, plus a 1230 that decodes cleanly but advertises itself as
  // not valid (code-phase-bias indicator cleared). This must stay informational
  // and must not suppress correction availability.
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1006u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 1100);

  RtcmFrame invalid_1230 = MakeValidRtcmFrame(1230u, 1200);
  invalid_1230.payload = BuildRtcm1230Payload(42u,
                                              /*code_phase_bias_indicator=*/false,
                                              /*has_l1_ca_bias=*/true,
                                              /*has_l1_p_bias=*/false,
                                              /*has_l2_ca_bias=*/false,
                                              /*has_l2_p_bias=*/false,
                                              /*l1_ca_bias_raw=*/10,
                                              std::nullopt,
                                              std::nullopt,
                                              std::nullopt);
  monitor.ObserveFrame(invalid_1230);

  ctx.Expect(monitor.HasDecodedGlonassBias1230() && !monitor.LastGlonassBias1230Valid(),
             "a 1230 with the code-phase-bias indicator cleared should decode as not valid");

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 2000;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 10000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  const GnssHealthSummary health = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      monitor,
      options);
  ctx.Expect(health.correction_available,
             "a not-valid GLONASS 1230 must not suppress correction availability");
  ctx.Expect(!health.HasErrors(),
             "a not-valid GLONASS 1230 must not raise an error-level diagnostic");

  bool saw_info_1230 = false;
  for (const auto& event : health.events)
  {
    if (event.code == "rtcm.1230_not_valid")
    {
      saw_info_1230 = true;
      ctx.Expect(event.severity == GnssDiagnosticSeverity::kInfo,
                 "rtcm.1230_not_valid should be informational, not a warning");
    }
  }
  ctx.Expect(saw_info_1230, "a decoded-but-not-valid 1230 should still surface an informational note");
}

void TestCrcValidMalformedMessagesDoNotSatisfySemanticHealth(TestContext& ctx)
{
  RtcmCorrectionHealthOptions portable_options;
  portable_options.now_timestamp_ns = 2000;
  portable_options.stale_after_ns = 5000;
  portable_options.required_observation_window_ns = 5000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(portable_options);

  RtcmCorrectionMonitor malformed_only;
  const RtcmFrame malformed_base = MakeIntegrityValidFrame(MakeRtcmPayload(1005u), 1000);
  const RtcmFrame malformed_msm = MakeIntegrityValidFrame(MakeRtcmPayload(1077u), 1100);
  malformed_only.ObserveFrame(malformed_base);
  malformed_only.ObserveFrame(malformed_msm);
  const GnssHealthSummary malformed_health =
      universal_gnss_protocols::BuildRtcmCorrectionHealth(malformed_only, portable_options);

  ctx.Expect(malformed_base.checksum_status == ChecksumStatus::kValid &&
                 malformed_msm.checksum_status == ChecksumStatus::kValid,
             "UGA-008 fixtures must pass real RTCM CRC framing before semantic rejection");
  ctx.Expect(malformed_only.valid_frames() == 2u &&
                 malformed_only.MessageCount(1005u) == 1u &&
                 malformed_only.MessageCount(1077u) == 1u,
             "CRC-valid malformed messages should remain visible to frame-level flow accounting");
  ctx.Expect(malformed_only.BaseStationArpMalformedCount() == 1u &&
                 malformed_only.MsmMalformedCount() == 1u,
             "specialized semantic decoders should reject the malformed base and MSM payloads");
  ctx.Expect(!malformed_only.HasRequiredCorrectionMessages(portable_options),
             "malformed base plus malformed MSM must not satisfy portable correction requirements");
  ctx.Expect(!malformed_health.correction_available,
             "CRC-valid malformed correction content must not report correction availability");
  ctx.Expect(!malformed_health.parser_healthy,
             "malformed known RTCM payloads must make semantic parser health unhealthy");

  RtcmCorrectionMonitor unsupported_semantics;
  unsupported_semantics.ObserveFrame(
      MakeIntegrityValidFrame(MakeRtcmPayload(4095u), 1000));
  RtcmCorrectionHealthOptions unsupported_options;
  unsupported_options.now_timestamp_ns = 1100;
  unsupported_options.stale_after_ns = 5000;
  unsupported_options.required_observation_window_ns = 5000;
  unsupported_options.required_message_types = {4095u};
  ctx.Expect(unsupported_semantics.valid_frames() == 1u &&
                 unsupported_semantics.MessageCount(4095u) == 1u,
             "a CRC-valid unsupported RTCM type should remain visible to frame-level accounting");
  ctx.Expect(!unsupported_semantics.HasRequiredMessageTypes({4095u}) &&
                 !unsupported_semantics.HasRequiredCorrectionMessages(unsupported_options) &&
                 !universal_gnss_protocols::BuildRtcmCorrectionHealth(
                     unsupported_semantics, unsupported_options).correction_available,
             "a raw RTCM type without a semantic decoder must not satisfy semantic requirements");

  RtcmCorrectionMonitor valid_base_malformed_msm;
  valid_base_malformed_msm.ObserveFrame(MakeDecodedBaseFrame(42u, 1000));
  valid_base_malformed_msm.ObserveFrame(malformed_msm);
  ctx.Expect(!valid_base_malformed_msm.HasRequiredCorrectionMessages(portable_options),
             "valid base metadata must not combine with a malformed MSM to become healthy");
  valid_base_malformed_msm.ObserveFrame(MakeDecodedMsmFrame(42u, 1200));
  portable_options.now_timestamp_ns = 1300;
  ctx.Expect(valid_base_malformed_msm.HasRequiredCorrectionMessages(portable_options) &&
                 universal_gnss_protocols::BuildRtcmCorrectionHealth(
                     valid_base_malformed_msm, portable_options).correction_available,
             "a valid same-station MSM replacement should restore correction availability");

  RtcmCorrectionMonitor malformed_base_valid_msm;
  malformed_base_valid_msm.ObserveFrame(malformed_base);
  malformed_base_valid_msm.ObserveFrame(MakeDecodedMsmFrame(42u, 1100));
  ctx.Expect(!malformed_base_valid_msm.HasRequiredCorrectionMessages(portable_options),
             "malformed base metadata must not combine with a valid MSM to become healthy");
  malformed_base_valid_msm.ObserveFrame(MakeDecodedBaseFrame(42u, 1200));
  ctx.Expect(malformed_base_valid_msm.HasRequiredCorrectionMessages(portable_options),
             "a valid same-station base replacement should restore correction requirements");

  RtcmCorrectionMonitor semantic_timestamp;
  semantic_timestamp.ObserveFrame(MakeDecodedMsmFrame(42u, 1000));
  RtcmFrame late_malformed_msm = malformed_msm;
  late_malformed_msm.timestamp_ns = 5000;
  semantic_timestamp.ObserveFrame(late_malformed_msm);
  RtcmCorrectionHealthOptions msm_window_options;
  msm_window_options.now_timestamp_ns = 5500;
  msm_window_options.stale_after_ns = 5000;
  msm_window_options.required_observation_window_ns = 1000;
  msm_window_options.require_any_msm = true;
  ctx.Expect(semantic_timestamp.LastMsmTimestampNs() ==
                 std::optional<std::int64_t>(5000) &&
                 semantic_timestamp.LastDecodedMsmTimestampNs() ==
                     std::optional<std::int64_t>(1000),
             "frame-level and semantic MSM timestamps should remain independently observable");
  ctx.Expect(!semantic_timestamp.HasRequiredCorrectionMessages(msm_window_options) &&
                 !universal_gnss_protocols::BuildRtcmCorrectionHealth(
                     semantic_timestamp, msm_window_options).correction_available,
             "a late malformed MSM must not refresh the semantic requirement window");

  std::size_t malformed_msm_satisfying_requirements = 0u;
  for (std::uint16_t family = 107u; family <= 113u; ++family)
  {
    for (std::uint16_t variant = 1u; variant <= 7u; ++variant)
    {
      const std::uint16_t message_type = static_cast<std::uint16_t>(family * 10u + variant);
      RtcmCorrectionMonitor monitor;
      monitor.ObserveFrame(MakeIntegrityValidFrame(MakeRtcmPayload(message_type), 1000));
      RtcmCorrectionHealthOptions options;
      options.now_timestamp_ns = 1100;
      options.stale_after_ns = 5000;
      options.required_observation_window_ns = 5000;
      options.required_message_types = {message_type};
      if (monitor.HasRequiredCorrectionMessages(options))
      {
        ++malformed_msm_satisfying_requirements;
      }
      ctx.Expect(monitor.valid_frames() == 1u && monitor.MsmMalformedCount() == 1u,
                 "every recognized CRC-valid MSM family/variant should stay frame-valid but decode as malformed");
    }
  }
  ctx.Expect(malformed_msm_satisfying_requirements == 0u,
             "none of the 49 truncated MSM family/variant payloads may satisfy semantic requirements");
}

void TestMalformedStationContentCannotChangeOwnershipOrStaticMetadata(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveFrame(MakeDecodedBaseFrame(42u, 1000));
  monitor.ObserveFrame(MakeDecodedMsmFrame(42u, 1100));

  std::vector<std::uint8_t> truncated_1006;
  std::size_t bit_offset = 0u;
  AppendUnsignedBits(truncated_1006, bit_offset, 1006u, 12u);
  AppendUnsignedBits(truncated_1006, bit_offset, 99u, 12u);
  monitor.ObserveFrame(MakeIntegrityValidFrame(truncated_1006, 5000));

  RtcmCorrectionHealthOptions require_1006;
  require_1006.now_timestamp_ns = 5100;
  require_1006.stale_after_ns = 5000;
  require_1006.required_observation_window_ns = 5000;
  require_1006.required_message_types = {1006u};
  ctx.Expect(monitor.station_id() == std::optional<std::uint16_t>(42u),
             "a malformed station-bearing 1006 must not steal station ownership");
  ctx.Expect(monitor.last_base_station_arp().has_value() &&
                 monitor.last_base_station_arp()->station_id == 42u &&
                 monitor.LastBaseStationArpTimestampNs() ==
                     std::optional<std::int64_t>(1000),
             "a malformed 1006 must not replace or refresh retained static base metadata");
  ctx.Expect(monitor.HasSeenBasePosition1006() &&
                 !monitor.HasRequiredCorrectionMessages(require_1006),
             "a malformed 1006 may remain frame-visible but must not establish its semantic requirement");

  monitor.ObserveFrame(MakeIntegrityValidFrame(BuildRtcm1006Payload(42u), 5200));
  ctx.Expect(monitor.station_id() == std::optional<std::uint16_t>(42u) &&
                 monitor.HasSeenBasePosition1006() &&
                 monitor.HasRequiredCorrectionMessages(require_1006),
             "a valid same-station 1006 replacement should establish metadata and its requirement");
}

void TestMaskDeclaredMsmTruncationCannotSatisfyHealthOrChangeOwnership(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveFrame(MakeDecodedBaseFrame(42u, 1000));
  monitor.ObserveFrame(MakeDecodedMsmFrame(42u, 1100));

  RtcmFrame truncated_foreign_msm = MakeValidRtcmFrame(1077u, 5000);
  truncated_foreign_msm.payload = BuildRtcmMsmPayload(1077u,
                                                       99u,
                                                       {1u},
                                                       {1u},
                                                       {true});
  truncated_foreign_msm.payload.resize(22u);
  truncated_foreign_msm = MakeIntegrityValidFrame(truncated_foreign_msm.payload, 5000);
  monitor.ObserveFrame(truncated_foreign_msm);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 5100;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 1000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  ctx.Expect(truncated_foreign_msm.checksum_status == ChecksumStatus::kValid &&
                 monitor.valid_frames() == 3u && monitor.MessageCount(1077u) == 2u,
             "an MSM truncated after its masks should remain CRC-valid and frame-visible");
  ctx.Expect(monitor.MsmMalformedCount() == 1u &&
                 monitor.LastDecodedMsmTimestampNs() ==
                     std::optional<std::int64_t>(1100),
             "mask-declared MSM data truncation should fail semantic decoding without refreshing it");
  ctx.Expect(monitor.station_id() == std::optional<std::uint16_t>(42u) &&
                 monitor.last_base_station_arp().has_value() &&
                 monitor.last_base_station_arp()->station_id == 42u,
             "a mask-valid but body-truncated foreign MSM must not steal ownership or clear static metadata");
  ctx.Expect(!monitor.HasRequiredCorrectionMessages(options) &&
                 !universal_gnss_protocols::BuildRtcmCorrectionHealth(monitor, options)
                      .correction_available,
             "a late mask-valid but body-truncated MSM must not refresh semantic correction health");
}

void TestMsmWithoutObservationCellsCannotSatisfyHealthOrChangeOwnership(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveFrame(MakeDecodedBaseFrame(42u, 1000));

  RtcmFrame empty_msm = MakeValidRtcmFrame(1077u, 1100);
  empty_msm.payload = BuildRtcmMsmPayload(1077u,
                                          42u,
                                          {1u},
                                          {1u},
                                          {false});
  empty_msm = MakeIntegrityValidFrame(empty_msm.payload, 1100);
  monitor.ObserveFrame(empty_msm);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 1200;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 1000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);
  const GnssHealthSummary empty_health =
      universal_gnss_protocols::BuildRtcmCorrectionHealth(monitor, options);

  ctx.Expect(empty_msm.checksum_status == ChecksumStatus::kValid &&
                 monitor.valid_frames() == 2u && monitor.MsmDecodeSuccessCount() == 1u &&
                 monitor.MsmMalformedCount() == 0u,
             "a complete MSM without populated cells should remain frame-valid and structurally decoded");
  ctx.Expect(empty_health.parser_healthy &&
                 !monitor.HasRequiredCorrectionMessages(options) &&
                 !empty_health.correction_available,
             "a structurally valid MSM without observations should not satisfy semantic correction health");

  monitor.ObserveFrame(MakeDecodedMsmFrame(42u, 1300));
  options.now_timestamp_ns = 1400;
  ctx.Expect(monitor.HasRequiredCorrectionMessages(options) &&
                 universal_gnss_protocols::BuildRtcmCorrectionHealth(monitor, options)
                     .correction_available,
             "a populated same-station MSM should restore semantic correction health");

  RtcmFrame empty_foreign_msm = MakeValidRtcmFrame(1077u, 5000);
  empty_foreign_msm.payload = BuildRtcmMsmPayload(1077u,
                                                  99u,
                                                  {1u},
                                                  {1u},
                                                  {false});
  empty_foreign_msm = MakeIntegrityValidFrame(empty_foreign_msm.payload, 5000);
  monitor.ObserveFrame(empty_foreign_msm);
  options.now_timestamp_ns = 5100;

  ctx.Expect(monitor.station_id() == std::optional<std::uint16_t>(42u) &&
                 monitor.last_base_station_arp().has_value() &&
                 monitor.last_base_station_arp()->station_id == 42u &&
                 monitor.LastDecodedMsmTimestampNs() ==
                     std::optional<std::int64_t>(1300),
             "an observation-empty foreign MSM must not mix station-owned decoded state");
  ctx.Expect(!monitor.HasRequiredCorrectionMessages(options) &&
                 !universal_gnss_protocols::BuildRtcmCorrectionHealth(monitor, options)
                      .correction_available,
             "an observation-empty foreign MSM must not refresh a stale semantic observation window");

  RtcmCorrectionMonitor unowned_empty_msm;
  unowned_empty_msm.ObserveFrame(empty_foreign_msm);
  unowned_empty_msm.ObserveFrame(MakeDecodedBaseFrame(42u, 5200));
  ctx.Expect(unowned_empty_msm.station_id() == std::optional<std::uint16_t>(42u) &&
                 !unowned_empty_msm.last_msm_summary().has_value(),
             "foreign observation-empty MSM state seen before ownership must be discarded when another station is established");
}

void TestRequired1230UsesValidSemanticContentWithoutBecomingUniversal(TestContext& ctx)
{
  RtcmCorrectionHealthOptions explicit_1230;
  explicit_1230.now_timestamp_ns = 2000;
  explicit_1230.stale_after_ns = 5000;
  explicit_1230.required_observation_window_ns = 5000;
  explicit_1230.require_glonass_bias = true;

  RtcmCorrectionMonitor malformed;
  malformed.ObserveFrame(MakeIntegrityValidFrame(MakeRtcmPayload(1230u), 1000));
  const GnssHealthSummary malformed_health =
      universal_gnss_protocols::BuildRtcmCorrectionHealth(malformed, explicit_1230);
  ctx.Expect(malformed.valid_frames() == 1u &&
                 malformed.GlonassBias1230MalformedCount() == 1u,
             "a truncated 1230 should remain CRC-valid at frame level and fail semantic decode");
  ctx.Expect(!malformed.HasRequiredCorrectionMessages(explicit_1230) &&
                 !malformed_health.correction_available &&
                 !malformed_health.parser_healthy,
             "a malformed 1230 must not satisfy an explicitly configured GLONASS-bias requirement");

  RtcmFrame valid_1230 = MakeIntegrityValidFrame(
      BuildRtcm1230Payload(42u,
                           true,
                           true,
                           false,
                           false,
                           false,
                           10,
                           std::nullopt,
                           std::nullopt,
                           std::nullopt),
      2100);
  malformed.ObserveFrame(valid_1230);
  explicit_1230.now_timestamp_ns = 2200;
  ctx.Expect(malformed.HasRequiredCorrectionMessages(explicit_1230) &&
                 universal_gnss_protocols::BuildRtcmCorrectionHealth(
                     malformed, explicit_1230).correction_available,
             "a decoded and validity-marked 1230 replacement should restore an explicit requirement");

  RtcmCorrectionMonitor invalid_indicator;
  invalid_indicator.ObserveFrame(MakeIntegrityValidFrame(
      BuildRtcm1230Payload(99u,
                           false,
                           true,
                           false,
                           false,
                           false,
                           10,
                           std::nullopt,
                           std::nullopt,
                           std::nullopt),
      1000));
  ctx.Expect(invalid_indicator.HasDecodedGlonassBias1230() &&
                 !invalid_indicator.LastGlonassBias1230Valid(),
             "a 1230 validity indicator clear should remain decoded for informational diagnostics");
  ctx.Expect(!invalid_indicator.HasRequiredCorrectionMessages(explicit_1230) &&
                 !invalid_indicator.station_id().has_value(),
             "a decoded-but-invalid 1230 must neither satisfy an explicit requirement nor establish ownership");

  invalid_indicator.ObserveFrame(MakeDecodedBaseFrame(42u, 1100));
  ctx.Expect(invalid_indicator.station_id() == std::optional<std::uint16_t>(42u) &&
                 !invalid_indicator.last_glonass_code_phase_bias().has_value(),
             "foreign invalid 1230 state seen before ownership must be discarded when another station is established");

  RtcmCorrectionMonitor portable;
  portable.ObserveFrame(MakeDecodedBaseFrame(42u, 1000));
  portable.ObserveFrame(MakeDecodedMsmFrame(42u, 1100));
  portable.ObserveFrame(MakeIntegrityValidFrame(
      BuildRtcm1230Payload(99u,
                           false,
                           true,
                           false,
                           false,
                           false,
                           10,
                           std::nullopt,
                           std::nullopt,
                           std::nullopt),
      1200));
  RtcmCorrectionHealthOptions portable_options;
  portable_options.now_timestamp_ns = 1300;
  portable_options.stale_after_ns = 5000;
  portable_options.required_observation_window_ns = 5000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(portable_options);
  ctx.Expect(!portable_options.require_glonass_bias &&
                 portable.station_id() == std::optional<std::uint16_t>(42u) &&
                 !portable.last_glonass_code_phase_bias().has_value() &&
                 portable.HasRequiredCorrectionMessages(portable_options) &&
                 universal_gnss_protocols::BuildRtcmCorrectionHealth(
                     portable, portable_options).correction_available,
             "optional invalid foreign-station 1230 content must not mix ownership state or suppress portable corrections");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestMessageCountsAndLastSeen(ctx);
  TestRateHelpers(ctx);
  TestTimestampHistoryRetention(ctx);
  TestMsmConstellationTracking(ctx);
  TestBasePositionAndGlonassBiasTracking(ctx);
  TestInvalidFrameHandling(ctx);
  TestGlonassBiasDecodeTracking(ctx);
  TestBaseStationArpSemanticObservationFromCaptured1006(ctx);
  TestMsmDecodeTracking(ctx);
  TestMsmSemanticObservations(ctx);
  TestMsmMalformedHealthEvent(ctx);
  TestHealthStates(ctx);
  TestFutureTimestampsCannotSatisfyFreshness(ctx);
  TestPortableRtkRequirementsAccept1006(ctx);
  TestPortableRtkRequirementsDoNotRequireGlonassBias(ctx);
  TestDecodedInvalid1230IsInformationalAndCorrectionStillAvailable(ctx);
  TestPortableRtkRequirementsUseRecentObservationWindow(ctx);
  TestStaticBaseMetadataOutlivesDynamicObservationWindow(ctx);
  TestStationOwnershipPreventsMixedCorrectionHealth(ctx);
  TestPortableRtkRequirementsRespectStartupGrace(ctx);
  TestCrcValidMalformedMessagesDoNotSatisfySemanticHealth(ctx);
  TestMalformedStationContentCannotChangeOwnershipOrStaticMetadata(ctx);
  TestMaskDeclaredMsmTruncationCannotSatisfyHealthOrChangeOwnership(ctx);
  TestMsmWithoutObservationCellsCannotSatisfyHealthOrChangeOwnership(ctx);
  TestRequired1230UsesValidSemanticContentWithoutBecomingUniversal(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols RTCM correction monitor tests passed\n";
  return EXIT_SUCCESS;
}
