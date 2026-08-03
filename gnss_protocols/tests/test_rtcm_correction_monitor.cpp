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
                                             7u,
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
                   monitor.last_msm_summary()->station_id == 7u &&
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
                                             7u,
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
      if (field.key == "station_id" && field.value == "7")
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

void TestPortableRtkRequirementsExpireStaleMsmObservations(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  // Base position + GLONASS bias seen once (sticky), but the per-epoch MSM
  // observations are all older than the recent window: a genuinely stalled
  // observation feed must still raise the missing-message error.
  monitor.ObserveMessage(MakeMessageInfo(1005u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1230u), 1100);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 9000);
  monitor.ObserveMessage(MakeMessageInfo(1087u), 9100);
  monitor.ObserveMessage(MakeMessageInfo(1097u), 9200);
  monitor.ObserveMessage(MakeMessageInfo(1127u), 9300);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 12000;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 2000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  const GnssHealthSummary health = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      monitor,
      options);
  ctx.Expect(
      !monitor.HasRequiredCorrectionMessages(options),
      "portable RTK requirements should expire stale MSM observations outside the recent window");
  ctx.Expect(health.overall_severity == GnssDiagnosticSeverity::kError,
             "missing recent MSM observations should remain an error after startup grace");
}

void TestPortableRtkRequirementsKeepStaleBasePosition(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  // Base position (1005) and GLONASS bias (1230) are static reference-station
  // metadata broadcast on a long cadence: they were last seen well outside the
  // recent observation window, yet the MSM observations are fresh. The sticky
  // base-position/bias rule must keep the requirements satisfied so a healthy
  // caster does not flap into a spurious required_messages_missing error.
  monitor.ObserveMessage(MakeMessageInfo(1006u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1230u), 1100);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 11000);
  monitor.ObserveMessage(MakeMessageInfo(1087u), 11100);
  monitor.ObserveMessage(MakeMessageInfo(1097u), 11200);
  monitor.ObserveMessage(MakeMessageInfo(1127u), 11300);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 12000;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 2000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  const GnssHealthSummary health =
      universal_gnss_protocols::BuildRtcmCorrectionHealth(monitor, options);
  ctx.Expect(
      monitor.HasRequiredCorrectionMessages(options),
      "stationary base position seen once should remain satisfied despite the recent window");
  ctx.Expect(health.overall_severity == GnssDiagnosticSeverity::kOk,
             "fresh MSM observations with a once-seen base position should clear the diagnostic");
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

void TestResetStreamStatePreservesStickyAcrossReconnect(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  // Simulate a healthy connection: base position, GLONASS bias and MSM seen.
  monitor.ObserveMessage(MakeMessageInfo(1006u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1230u), 1100);
  RtcmFrame msm_frame = MakeValidRtcmFrame(1077u, 1200);
  msm_frame.payload = BuildRtcmMsmPayload(1077u, 7u, {1u, 3u}, {2u}, {true, false});
  monitor.ObserveFrame(msm_frame);

  ctx.Expect(monitor.HasSeenBasePositionMessage(), "base position should be seen before reconnect");
  ctx.Expect(monitor.HasSeenGlonassBias1230(), "GLONASS bias should be seen before reconnect");
  ctx.Expect(monitor.MessageCount(1077u) == 1u,
             "MSM observation should be counted before reconnect");
  ctx.Expect(monitor.last_msm_summary().has_value(),
             "MSM semantic state should exist before reconnect");

  // Same-endpoint reconnect: clear per-stream observations while retaining the
  // static base-station metadata.
  monitor.ResetStreamState();
  ctx.Expect(monitor.MessageCount(1077u) == 0u,
             "ResetStreamState should clear per-stream observation counters");
  ctx.Expect(monitor.total_frames() == 0u, "ResetStreamState should clear the frame counters");
  ctx.Expect(!monitor.last_msm_summary().has_value(),
             "ResetStreamState should clear per-stream MSM semantic state");
  ctx.Expect(monitor.HasSeenBasePositionMessage(),
             "ResetStreamState should preserve the sticky base-position flag");
  ctx.Expect(monitor.HasSeenGlonassBias1230(),
             "ResetStreamState should preserve the sticky GLONASS-bias flag");

  // Endpoint change / full reset drops the sticky metadata.
  monitor.Reset();
  ctx.Expect(!monitor.HasSeenBasePositionMessage(),
             "Reset should clear the sticky base-position flag on an endpoint change");
  ctx.Expect(!monitor.HasSeenGlonassBias1230(),
             "Reset should clear the sticky GLONASS-bias flag on an endpoint change");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestMessageCountsAndLastSeen(ctx);
  TestRateHelpers(ctx);
  TestMsmConstellationTracking(ctx);
  TestBasePositionAndGlonassBiasTracking(ctx);
  TestInvalidFrameHandling(ctx);
  TestGlonassBiasDecodeTracking(ctx);
  TestBaseStationArpSemanticObservationFromCaptured1006(ctx);
  TestMsmDecodeTracking(ctx);
  TestMsmSemanticObservations(ctx);
  TestMsmMalformedHealthEvent(ctx);
  TestHealthStates(ctx);
  TestPortableRtkRequirementsAccept1006(ctx);
  TestPortableRtkRequirementsDoNotRequireGlonassBias(ctx);
  TestDecodedInvalid1230IsInformationalAndCorrectionStillAvailable(ctx);
  TestPortableRtkRequirementsExpireStaleMsmObservations(ctx);
  TestPortableRtkRequirementsKeepStaleBasePosition(ctx);
  TestPortableRtkRequirementsRespectStartupGrace(ctx);
  TestResetStreamStatePreservesStickyAcrossReconnect(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols RTCM correction monitor tests passed\n";
  return EXIT_SUCCESS;
}
