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

}  // namespace

int main()
{
  TestContext ctx;

  TestMessageTypeExtraction(ctx);
  TestTruncatedPayloadHandling(ctx);
  TestClassificationHelpers(ctx);
  TestFrameParsingBehavior(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols RTCM parser tests passed\n";
  return EXIT_SUCCESS;
}
