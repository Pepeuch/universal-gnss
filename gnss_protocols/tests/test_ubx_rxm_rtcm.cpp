#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/ubx_parser.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"

namespace
{

using universal_gnss::GnssDiagnosticCategory;
using universal_gnss::GnssDiagnosticSeverity;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_protocols::UbxRxmRtcmMessageUse;

struct TestContext
{
  int failures{0};

  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

void WriteLeU2(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::uint16_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

UbxFrame BuildUbxFrame(const std::uint8_t class_id,
                       const std::uint8_t message_id,
                       const std::vector<std::uint8_t>& payload,
                       const std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  std::vector<std::uint8_t> bytes;
  bytes.reserve(6u + payload.size() + 2u);
  bytes.push_back(0xB5u);
  bytes.push_back(0x62u);
  bytes.push_back(class_id);
  bytes.push_back(message_id);
  bytes.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((payload.size() >> 8u) & 0xFFu));
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const auto checksum =
      universal_gnss_protocols::ComputeUbxChecksum(bytes.data() + 2u, bytes.size() - 2u);
  bytes.push_back(checksum.ck_a);
  bytes.push_back(checksum.ck_b);

  UbxFrameFramer framer;
  universal_gnss_protocols::ParserResult<UbxFrame> result;
  for (const auto byte : bytes)
  {
    result = framer.PushByte(byte, timestamp_ns);
  }

  if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
  {
    std::cerr << "FAILED: test setup could not frame UBX message\n";
    std::exit(EXIT_FAILURE);
  }

  return *result.record;
}

std::vector<std::uint8_t> MakeRxmRtcmPayload(const std::uint8_t flags,
                                             const std::uint16_t subtype,
                                             const std::uint16_t ref_station_id,
                                             const std::uint16_t message_type)
{
  std::vector<std::uint8_t> payload(8u, 0u);
  payload[0u] = 0x02u;
  payload[1u] = flags;
  WriteLeU2(payload, 2u, subtype);
  WriteLeU2(payload, 4u, ref_station_id);
  WriteLeU2(payload, 6u, message_type);
  return payload;
}

void TestValidAcceptedRtcmParsing(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxRxmRtcm(
      BuildUbxFrame(0x02u, 0x32u, MakeRxmRtcmPayload(0x04u, 0u, 42u, 1077u), 1234));

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid RXM-RTCM frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(1234),
             "RXM-RTCM should preserve framing timestamps");
  ctx.Expect(record.version == 0x02u, "RXM-RTCM should decode version 0x02");
  ctx.Expect(!record.crc_failed && record.crc_ok,
             "RXM-RTCM should decode a passing receiver-side CRC state");
  ctx.Expect(record.message_use == UbxRxmRtcmMessageUse::kUsed &&
                 record.message_used && record.message_use_known,
             "RXM-RTCM should decode the msgUsed field");
  ctx.Expect(record.sub_type == 0u && record.ref_station_id == 42u &&
                 record.message_type == 1077u,
             "RXM-RTCM should decode subtype, reference station, and RTCM message type");
}

void TestValidBaseMessageParsing(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxRxmRtcm(
      BuildUbxFrame(0x02u, 0x32u, MakeRxmRtcmPayload(0x02u, 0u, 7u, 1005u)));

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid RXM-RTCM 1005 frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.message_type == 1005u,
             "RXM-RTCM should preserve base-position RTCM message types");
  ctx.Expect(record.message_use == UbxRxmRtcmMessageUse::kNotUsed &&
                 !record.message_used && record.message_use_known,
             "RXM-RTCM should distinguish received-but-not-used messages");
}

void TestCrcFailedStatusAndDiagnostics(TestContext& ctx)
{
  const auto parsed = universal_gnss_protocols::ParseUbxRxmRtcm(
      BuildUbxFrame(0x02u, 0x32u, MakeRxmRtcmPayload(0x01u, 0u, 0xFFFFu, 1077u), 2222));

  ctx.Expect(parsed.record.has_value(),
             "CRC-failed diagnostics test requires a parsed RXM-RTCM record");
  if (!parsed.record.has_value())
  {
    return;
  }

  const auto event = universal_gnss_protocols::UbxRxmRtcmToDiagnosticEvent(*parsed.record);
  ctx.Expect(parsed.record->crc_failed && !parsed.record->crc_ok,
             "RXM-RTCM should expose the receiver-side CRC failed flag");
  ctx.Expect(event.category == GnssDiagnosticCategory::kCorrection &&
                 event.severity == GnssDiagnosticSeverity::kWarning &&
                 event.code == "ubx_rxm_rtcm.crc_failed" &&
                 event.timestamp_ns == std::optional<std::int64_t>(2222),
             "RXM-RTCM CRC failures should become portable correction warnings");
  ctx.Expect(event.source.has_value() && *event.source == "ubx.rxm_rtcm",
             "RXM-RTCM diagnostics should declare a stable portable source");
}

void TestDiagnosticSeverityAndNoRuntimeInference(TestContext& ctx)
{
  const auto accepted = universal_gnss_protocols::ParseUbxRxmRtcm(
      BuildUbxFrame(0x02u, 0x32u, MakeRxmRtcmPayload(0x04u, 0u, 11u, 1077u)));
  ctx.Expect(accepted.record.has_value(),
             "accepted RXM-RTCM diagnostic test requires a parsed record");
  if (!accepted.record.has_value())
  {
    return;
  }

  const auto accepted_event =
      universal_gnss_protocols::UbxRxmRtcmToDiagnosticEvent(*accepted.record);
  ctx.Expect(accepted_event.severity == GnssDiagnosticSeverity::kOk &&
                 accepted_event.category == GnssDiagnosticCategory::kCorrection,
             "accepted RXM-RTCM should map to a correction-ok diagnostic only");

  const auto not_used = universal_gnss_protocols::ParseUbxRxmRtcm(
      BuildUbxFrame(0x02u, 0x32u, MakeRxmRtcmPayload(0x02u, 0u, 11u, 1005u)));
  ctx.Expect(not_used.record.has_value(),
             "not-used RXM-RTCM diagnostic test requires a parsed record");
  if (!not_used.record.has_value())
  {
    return;
  }

  const auto not_used_event =
      universal_gnss_protocols::UbxRxmRtcmToDiagnosticEvent(*not_used.record);
  ctx.Expect(not_used_event.severity == GnssDiagnosticSeverity::kWarning &&
                 not_used_event.category == GnssDiagnosticCategory::kCorrection &&
                 not_used_event.code == "ubx_rxm_rtcm.not_used",
             "not-used RXM-RTCM should map to a correction warning without inventing runtime fix state");
}

void TestWrongClassIdAndMalformedPayloads(TestContext& ctx)
{
  const UbxFrame wrong_message = BuildUbxFrame(0x01u, 0x32u, std::vector<std::uint8_t>(8u, 0u));
  ctx.Expect(universal_gnss_protocols::ParseUbxRxmRtcm(wrong_message).status ==
                 ParserStatus::kSkipped,
             "wrong UBX class/id should be skipped for RXM-RTCM");

  const UbxFrame short_payload = BuildUbxFrame(0x02u, 0x32u, std::vector<std::uint8_t>(7u, 0u));
  ctx.Expect(universal_gnss_protocols::ParseUbxRxmRtcm(short_payload).status ==
                 ParserStatus::kInvalidData,
             "wrong RXM-RTCM payload length should be rejected");

  auto wrong_version_payload = MakeRxmRtcmPayload(0x04u, 0u, 42u, 1077u);
  wrong_version_payload[0u] = 0x01u;
  const UbxFrame wrong_version = BuildUbxFrame(0x02u, 0x32u, wrong_version_payload);
  ctx.Expect(universal_gnss_protocols::ParseUbxRxmRtcm(wrong_version).status ==
                 ParserStatus::kInvalidData,
             "unexpected RXM-RTCM payload versions should be rejected");

  UbxFrame invalid_checksum = BuildUbxFrame(0x02u, 0x32u, MakeRxmRtcmPayload(0x04u, 0u, 42u, 1077u));
  invalid_checksum.checksum_status = ChecksumStatus::kInvalid;
  ctx.Expect(universal_gnss_protocols::ParseUbxRxmRtcm(invalid_checksum).status ==
                 ParserStatus::kInvalidData,
             "invalid checksum status should reject RXM-RTCM records");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValidAcceptedRtcmParsing(ctx);
  TestValidBaseMessageParsing(ctx);
  TestCrcFailedStatusAndDiagnostics(ctx);
  TestDiagnosticSeverityAndNoRuntimeInference(ctx);
  TestWrongClassIdAndMalformedPayloads(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols UBX-RXM-RTCM tests passed\n";
  return EXIT_SUCCESS;
}
