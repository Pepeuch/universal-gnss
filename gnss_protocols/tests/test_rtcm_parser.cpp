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

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols RTCM parser tests passed\n";
  return EXIT_SUCCESS;
}
