#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"
#include "universal_gnss_protocols/rtcm_records.hpp"

namespace
{

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::RtcmConstellation;
using universal_gnss_protocols::RtcmFrame;

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

void AppendText(std::vector<std::uint8_t>& payload,
                std::size_t& bit_offset,
                const std::string& value)
{
  for (const char character : value)
  {
    AppendUnsignedBits(payload, bit_offset, static_cast<std::uint8_t>(character), 8u);
  }
}

std::vector<std::uint8_t> BuildRtcmAntennaDescriptorPayload(
    const std::uint16_t message_type,
    const std::uint16_t station_id,
    const std::string& descriptor,
    const std::uint8_t setup_id,
    const std::optional<std::string>& serial_number = std::nullopt)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;
  AppendUnsignedBits(payload, bit_offset, message_type, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, descriptor.size(), 8u);
  AppendText(payload, bit_offset, descriptor);
  AppendUnsignedBits(payload, bit_offset, setup_id, 8u);
  if (message_type == 1008u)
  {
    const std::string serial = serial_number.value_or("");
    AppendUnsignedBits(payload, bit_offset, serial.size(), 8u);
    AppendText(payload, bit_offset, serial);
  }
  return payload;
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
                               const std::size_t cell_count)
{
  switch (msm_variant)
  {
    case 1u:
      return satellite_count * 10u + cell_count * 15u;
    case 2u:
      return satellite_count * 10u + cell_count * 27u;
    case 3u:
      return satellite_count * 10u + cell_count * 42u;
    case 4u:
      return satellite_count * 18u + cell_count * 48u;
    case 5u:
      return satellite_count * 36u + cell_count * 63u;
    case 6u:
      return satellite_count * 18u + cell_count * 65u;
    case 7u:
      return satellite_count * 36u + cell_count * 80u;
    default:
      return 0u;
  }
}

std::vector<std::uint8_t> BuildRtcmMsmPayload(const std::uint16_t message_type,
                                              const std::uint16_t station_id,
                                              const std::uint32_t epoch_time,
                                              const bool multiple_message,
                                              const std::uint8_t issue_of_data_station,
                                              const std::uint8_t session_transmission_time,
                                              const std::uint8_t clock_steering_indicator,
                                              const std::uint8_t external_clock_indicator,
                                              const bool divergence_free_smoothing,
                                              const std::uint8_t smoothing_interval,
                                              const std::vector<std::uint8_t>& satellite_ids,
                                              const std::vector<std::uint8_t>& signal_ids,
                                              const std::vector<bool>& cell_mask)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;

  AppendUnsignedBits(payload, bit_offset, message_type, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, epoch_time, 30u);
  AppendUnsignedBits(payload, bit_offset, multiple_message ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, issue_of_data_station, 3u);
  AppendUnsignedBits(payload, bit_offset, session_transmission_time, 7u);
  AppendUnsignedBits(payload, bit_offset, clock_steering_indicator, 2u);
  AppendUnsignedBits(payload, bit_offset, external_clock_indicator, 2u);
  AppendUnsignedBits(payload, bit_offset, divergence_free_smoothing ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, smoothing_interval, 3u);

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

  for (const bool present : cell_mask)
  {
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
  }

  std::size_t populated_cells = 0u;
  for (const bool present : cell_mask)
  {
    if (present)
    {
      ++populated_cells;
    }
  }

  AppendZeroBits(payload,
                 bit_offset,
                 GetRtcmMsmBodyBits(universal_gnss_protocols::GetRtcmMsmVariant(message_type),
                                    satellite_ids.size(),
                                    populated_cells));
  return payload;
}

RtcmFrame MakeValidRtcmFrame(const std::uint16_t message_type,
                             const std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  RtcmFrame frame;
  frame.timestamp_ns = timestamp_ns;
  frame.payload = MakeRtcmPayload(message_type);
  frame.checksum_status = ChecksumStatus::kValid;
  frame.message_type = 0u;
  return frame;
}

void TestMessageTypeExtraction(TestContext& ctx)
{
  ctx.Expect(universal_gnss_protocols::ExtractRtcmMessageType(MakeRtcmPayload(1005u)) ==
                 std::optional<std::uint16_t>(1005u),
             "RTCM helper should extract message type 1005");
  ctx.Expect(universal_gnss_protocols::ExtractRtcmMessageType(MakeRtcmPayload(1077u)) ==
                 std::optional<std::uint16_t>(1077u),
             "RTCM helper should extract message type 1077");
  ctx.Expect(universal_gnss_protocols::ExtractRtcmMessageType(MakeRtcmPayload(1087u)) ==
                 std::optional<std::uint16_t>(1087u),
             "RTCM helper should extract message type 1087");
  ctx.Expect(universal_gnss_protocols::ExtractRtcmMessageType(MakeRtcmPayload(1097u)) ==
                 std::optional<std::uint16_t>(1097u),
             "RTCM helper should extract message type 1097");
  ctx.Expect(universal_gnss_protocols::ExtractRtcmMessageType(MakeRtcmPayload(1127u)) ==
                 std::optional<std::uint16_t>(1127u),
             "RTCM helper should extract message type 1127");
}

void TestTruncatedPayloadHandling(TestContext& ctx)
{
  const std::vector<std::uint8_t> truncated = {0x3Eu};
  ctx.Expect(!universal_gnss_protocols::ExtractRtcmMessageType(truncated).has_value(),
             "RTCM helper should reject a truncated payload");

  RtcmFrame frame = MakeValidRtcmFrame(1005u);
  frame.payload = truncated;
  ctx.Expect(universal_gnss_protocols::ParseRtcmMessageInfo(frame).status ==
                 ParserStatus::kInvalidData,
             "RTCM semantic parser should reject a truncated frame payload");
}

void TestClassificationHelpers(TestContext& ctx)
{
  ctx.Expect(universal_gnss_protocols::IsRtcmStationArpMessage(1005u),
             "1005 should classify as a station ARP message");
  ctx.Expect(universal_gnss_protocols::IsRtcmStationArpMessage(1006u),
             "1006 should classify as a station ARP message");
  ctx.Expect(universal_gnss_protocols::IsRtcmGlonassBiasMessage(1230u),
             "1230 should classify as a GLONASS bias message");
  ctx.Expect(universal_gnss_protocols::IsRtcmMsmMessage(1077u),
             "1077 should classify as an MSM message");
  ctx.Expect(universal_gnss_protocols::IsRtcmMsmMessage(1087u),
             "1087 should classify as an MSM message");
  ctx.Expect(universal_gnss_protocols::IsRtcmMsmMessage(1097u),
             "1097 should classify as an MSM message");
  ctx.Expect(universal_gnss_protocols::IsRtcmMsmMessage(1127u),
             "1127 should classify as an MSM message");
  ctx.Expect(universal_gnss_protocols::GetRtcmMsmConstellation(1077u) ==
                 RtcmConstellation::kGps,
             "1077 should classify as GPS MSM");
  ctx.Expect(universal_gnss_protocols::GetRtcmMsmConstellation(1087u) ==
                 RtcmConstellation::kGlonass,
             "1087 should classify as GLONASS MSM");
  ctx.Expect(universal_gnss_protocols::GetRtcmMsmConstellation(1097u) ==
                 RtcmConstellation::kGalileo,
             "1097 should classify as Galileo MSM");
  ctx.Expect(universal_gnss_protocols::GetRtcmMsmConstellation(1127u) ==
                 RtcmConstellation::kBeiDou,
             "1127 should classify as BeiDou MSM");
  ctx.Expect(universal_gnss_protocols::GetRtcmMsmConstellation(1137u) ==
                 RtcmConstellation::kNavIc,
             "1137 should classify as NavIC MSM");
  ctx.Expect(!universal_gnss_protocols::IsRtcmMsmMessage(1005u),
             "1005 should not classify as an MSM message");
}

void TestFrameParsingBehavior(TestContext& ctx)
{
  const auto info_1005 = universal_gnss_protocols::ParseRtcmMessageInfo(
      MakeValidRtcmFrame(1005u, 42));
  ctx.Expect(info_1005.status == ParserStatus::kRecordReady && info_1005.record.has_value(),
             "valid 1005 frame should parse successfully");
  if (info_1005.record.has_value())
  {
    ctx.Expect(info_1005.record->message_type == 1005u,
               "parsed RTCM info should expose the 1005 message type");
    ctx.Expect(info_1005.record->is_station_arp,
               "parsed RTCM info should classify 1005 as a station ARP message");
    ctx.Expect(!info_1005.record->is_msm,
               "parsed RTCM info should not classify 1005 as MSM");
  }

  const auto info_1077 = universal_gnss_protocols::ParseRtcmMessageInfo(
      MakeValidRtcmFrame(1077u));
  ctx.Expect(info_1077.status == ParserStatus::kRecordReady && info_1077.record.has_value(),
             "valid 1077 frame should parse successfully");
  if (info_1077.record.has_value())
  {
    ctx.Expect(info_1077.record->message_type == 1077u,
               "parsed RTCM info should expose the 1077 message type");
    ctx.Expect(info_1077.record->is_msm &&
                   info_1077.record->msm_constellation == RtcmConstellation::kGps,
               "parsed RTCM info should classify 1077 as GPS MSM");
  }

  RtcmFrame invalid_checksum = MakeValidRtcmFrame(1230u);
  invalid_checksum.checksum_status = ChecksumStatus::kInvalid;
  ctx.Expect(universal_gnss_protocols::ParseRtcmMessageInfo(invalid_checksum).status ==
                 ParserStatus::kInvalidData,
             "invalid checksum frames should be rejected");
}

void TestGlonassCodePhaseBiasParsing(TestContext& ctx)
{
  RtcmFrame frame = MakeValidRtcmFrame(1230u, 123456789LL);
  frame.payload = BuildRtcm1230Payload(42u,
                                       true,
                                       true,
                                       false,
                                       true,
                                       true,
                                       10,
                                       std::nullopt,
                                       -5,
                                       7);

  const auto parsed = universal_gnss_protocols::ParseRtcmGlonassCodePhaseBias(frame);
  ctx.Expect(parsed.status == ParserStatus::kRecordReady && parsed.record.has_value(),
             "valid RTCM 1230 payload should parse successfully");
  if (!parsed.record.has_value())
  {
    return;
  }

  ctx.Expect(parsed.record->message_type == 1230u && parsed.record->station_id == 42u,
             "parsed RTCM 1230 record should expose message type and station id");
  ctx.Expect(parsed.record->code_phase_bias_indicator && parsed.record->valid,
             "parsed RTCM 1230 record should preserve the bias indicator and validity");
  ctx.Expect(parsed.record->signal_mask == 0x0Du,
             "parsed RTCM 1230 record should preserve the L1/L2 signal mask");
  ctx.Expect(parsed.record->l1_ca_bias_m == std::optional<double>(0.2) &&
                 !parsed.record->l1_p_bias_m.has_value() &&
                 parsed.record->l2_ca_bias_m == std::optional<double>(-0.1) &&
                 parsed.record->l2_p_bias_m == std::optional<double>(0.14),
             "parsed RTCM 1230 record should scale signed bias values with optional fields");
}

void TestGlonassCodePhaseBiasRejectsTruncatedPayload(TestContext& ctx)
{
  RtcmFrame frame = MakeValidRtcmFrame(1230u);
  frame.payload = BuildRtcm1230Payload(42u,
                                       true,
                                       true,
                                       true,
                                       false,
                                       false,
                                       10,
                                       12,
                                       std::nullopt,
                                       std::nullopt);
  frame.payload.pop_back();

  const auto parsed = universal_gnss_protocols::ParseRtcmGlonassCodePhaseBias(frame);
  ctx.Expect(parsed.status == ParserStatus::kInvalidData,
             "truncated RTCM 1230 payloads should be rejected");
}

void TestAntennaDescriptorParsing(TestContext& ctx)
{
  RtcmFrame descriptor = MakeValidRtcmFrame(1007u);
  descriptor.payload = BuildRtcmAntennaDescriptorPayload(
      1007u, 42u, "TRM59800.00", 3u);
  const auto parsed_descriptor = universal_gnss_protocols::ParseRtcmAntennaDescriptor(descriptor);
  ctx.Expect(parsed_descriptor.status == ParserStatus::kRecordReady &&
                 parsed_descriptor.record.has_value(),
             "valid RTCM 1007 payload should parse successfully");
  if (parsed_descriptor.record.has_value())
  {
    ctx.Expect(parsed_descriptor.record->station_id == 42u &&
                   parsed_descriptor.record->antenna_descriptor == "TRM59800.00" &&
                   parsed_descriptor.record->antenna_setup_id == 3u &&
                   !parsed_descriptor.record->antenna_serial_number.has_value(),
               "RTCM 1007 should expose station-owned antenna descriptor metadata");
  }

  RtcmFrame serial = MakeValidRtcmFrame(1008u);
  serial.payload = BuildRtcmAntennaDescriptorPayload(
      1008u, 42u, "TRM59800.00", 3u, "12345");
  const auto parsed_serial = universal_gnss_protocols::ParseRtcmAntennaDescriptor(serial);
  ctx.Expect(parsed_serial.status == ParserStatus::kRecordReady &&
                 parsed_serial.record.has_value() &&
                 parsed_serial.record->antenna_serial_number == std::optional<std::string>("12345"),
             "RTCM 1008 should preserve the optional antenna serial number");
}

void TestAntennaDescriptorRejectsInvalidPayload(TestContext& ctx)
{
  RtcmFrame wrong_type = MakeValidRtcmFrame(1005u);
  ctx.Expect(universal_gnss_protocols::ParseRtcmAntennaDescriptor(wrong_type).status ==
                 ParserStatus::kInvalidData,
             "non-1007/1008 RTCM messages should not parse as antenna descriptors");

  RtcmFrame truncated = MakeValidRtcmFrame(1008u);
  truncated.payload = BuildRtcmAntennaDescriptorPayload(
      1008u, 42u, "TRM59800.00", 3u, "12345");
  truncated.payload.pop_back();
  ctx.Expect(universal_gnss_protocols::ParseRtcmAntennaDescriptor(truncated).status ==
                 ParserStatus::kInvalidData,
             "truncated RTCM 1008 descriptor fields should be rejected");

  RtcmFrame overlength = MakeValidRtcmFrame(1007u);
  overlength.payload = BuildRtcmAntennaDescriptorPayload(
      1007u, 42u, std::string(32u, 'A'), 3u);
  ctx.Expect(universal_gnss_protocols::ParseRtcmAntennaDescriptor(overlength).status ==
                 ParserStatus::kInvalidData,
             "RTCM antenna descriptor lengths above 31 characters should be rejected");
}

void TestGpsMsmSummaryParsing(TestContext& ctx)
{
  RtcmFrame frame = MakeValidRtcmFrame(1077u, 1000);
  frame.payload = BuildRtcmMsmPayload(1077u,
                                      42u,
                                      123456u,
                                      true,
                                      5u,
                                      17u,
                                      2u,
                                      1u,
                                      true,
                                      4u,
                                      {1u, 3u},
                                      {1u, 5u},
                                      {true, false, true, true});

  const auto parsed = universal_gnss_protocols::ParseRtcmMsmSummary(frame);
  ctx.Expect(parsed.status == ParserStatus::kRecordReady && parsed.record.has_value(),
             "valid GPS MSM7 payloads should expose an MSM summary");
  if (!parsed.record.has_value())
  {
    return;
  }

  ctx.Expect(parsed.record->message_type == 1077u &&
                 parsed.record->station_id == 42u &&
                 parsed.record->constellation == RtcmConstellation::kGps &&
                 parsed.record->msm_variant == 7u,
             "GPS MSM7 summary should preserve type, station id, constellation, and variant");
  ctx.Expect(parsed.record->multiple_message &&
                 parsed.record->issue_of_data_station == 5u &&
                 parsed.record->session_transmission_time == 17u &&
                 parsed.record->clock_steering_indicator == 2u &&
                 parsed.record->external_clock_indicator == 1u &&
                 parsed.record->divergence_free_smoothing &&
                 parsed.record->smoothing_interval == 4u,
             "GPS MSM7 summary should preserve common header metadata");
  ctx.Expect(parsed.record->satellite_count == 2u &&
                 parsed.record->signal_count == 2u &&
                 parsed.record->cell_count == 3u,
             "GPS MSM7 summary should count satellites, signals, and populated cells");
}

void TestGlonassMsmSummaryParsing(TestContext& ctx)
{
  RtcmFrame frame = MakeValidRtcmFrame(1087u, 2000);
  frame.payload = BuildRtcmMsmPayload(1087u,
                                      7u,
                                      654321u,
                                      false,
                                      2u,
                                      8u,
                                      0u,
                                      0u,
                                      false,
                                      3u,
                                      {2u},
                                      {1u, 3u, 4u},
                                      {true, false, true});

  const auto parsed = universal_gnss_protocols::ParseRtcmMsmSummary(frame);
  ctx.Expect(parsed.status == ParserStatus::kRecordReady && parsed.record.has_value(),
             "valid GLONASS MSM7 payloads should expose an MSM summary");
  if (!parsed.record.has_value())
  {
    return;
  }

  ctx.Expect(parsed.record->message_type == 1087u &&
                 parsed.record->station_id == 7u &&
                 parsed.record->constellation == RtcmConstellation::kGlonass &&
                 parsed.record->msm_variant == 7u,
             "GLONASS MSM7 summary should preserve type, station id, constellation, and variant");
  ctx.Expect(parsed.record->satellite_count == 1u &&
                 parsed.record->signal_count == 3u &&
                 parsed.record->cell_count == 2u,
             "GLONASS MSM7 summary should count satellites, signals, and populated cells");
}

void TestMsmSummaryRejectsTruncatedPayload(TestContext& ctx)
{
  for (std::uint8_t variant = 1u; variant <= 7u; ++variant)
  {
    const std::uint16_t message_type = static_cast<std::uint16_t>(1070u + variant);
    RtcmFrame frame = MakeValidRtcmFrame(message_type);
    frame.payload = BuildRtcmMsmPayload(message_type,
                                        42u,
                                        123456u,
                                        false,
                                        1u,
                                        1u,
                                        0u,
                                        0u,
                                        false,
                                        0u,
                                        {1u},
                                        {1u},
                                        {true});

    const auto complete = universal_gnss_protocols::ParseRtcmMsmSummary(frame);
    ctx.Expect(complete.status == ParserStatus::kRecordReady,
               "complete RTCM MSM1-7 payloads should expose a summary");

    frame.payload.pop_back();
    const auto truncated = universal_gnss_protocols::ParseRtcmMsmSummary(frame);
    ctx.Expect(truncated.status == ParserStatus::kInvalidData,
               "RTCM MSM1-7 payloads truncated inside mask-declared data should be rejected");
  }
}

void TestMsmSummaryRejectsOversizedCellMask(TestContext& ctx)
{
  RtcmFrame frame = MakeValidRtcmFrame(1077u);
  frame.payload = BuildRtcmMsmPayload(1077u,
                                      42u,
                                      123456u,
                                      false,
                                      1u,
                                      1u,
                                      0u,
                                      0u,
                                      false,
                                      0u,
                                      {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u},
                                      {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u},
                                      std::vector<bool>(72u, true));

  const auto parsed = universal_gnss_protocols::ParseRtcmMsmSummary(frame);
  ctx.Expect(parsed.status == ParserStatus::kInvalidData,
             "RTCM MSM cell masks larger than the protocol's 64-cell capacity should be rejected");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestMessageTypeExtraction(ctx);
  TestTruncatedPayloadHandling(ctx);
  TestClassificationHelpers(ctx);
  TestFrameParsingBehavior(ctx);
  TestGlonassCodePhaseBiasParsing(ctx);
  TestGlonassCodePhaseBiasRejectsTruncatedPayload(ctx);
  TestAntennaDescriptorParsing(ctx);
  TestAntennaDescriptorRejectsInvalidPayload(ctx);
  TestGpsMsmSummaryParsing(ctx);
  TestGlonassMsmSummaryParsing(ctx);
  TestMsmSummaryRejectsTruncatedPayload(ctx);
  TestMsmSummaryRejectsOversizedCellMask(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols RTCM parser tests passed\n";
  return EXIT_SUCCESS;
}
