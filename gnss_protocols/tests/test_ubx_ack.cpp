#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/ubx_parser.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"

namespace
{

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UbxAckMessageKind;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;

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

UbxFrame BuildUbxFrame(std::uint8_t class_id,
                       std::uint8_t message_id,
                       const std::vector<std::uint8_t>& payload,
                       const bool valid_checksum = true,
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
  bytes.push_back(valid_checksum ? checksum.ck_a : static_cast<std::uint8_t>(checksum.ck_a ^ 0x01u));
  bytes.push_back(checksum.ck_b);

  UbxFrameFramer framer;
  universal_gnss_protocols::ParserResult<UbxFrame> result;
  for (const auto byte : bytes)
  {
    result = framer.PushByte(byte, timestamp_ns);
  }

  if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
  {
    std::cerr << "FAILED: test setup could not frame UBX ACK message\n";
    std::exit(EXIT_FAILURE);
  }

  return *result.record;
}

void TestValidAckAckParsing(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxAck(
      BuildUbxFrame(0x05u, 0x01u, {0x06u, 0x8Au}, true, 1111));

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid ACK-ACK frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(1111),
             "ACK-ACK should preserve the framing timestamp");
  ctx.Expect(record.kind == UbxAckMessageKind::kAck &&
                 record.target_class_id == 0x06u &&
                 record.target_message_id == 0x8Au,
             "ACK-ACK should decode the acknowledged class/id payload");
}

void TestValidAckNakParsing(TestContext& ctx)
{
  const auto result =
      universal_gnss_protocols::ParseUbxAck(BuildUbxFrame(0x05u, 0x00u, {0x06u, 0x8Bu}));

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid ACK-NAK frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.kind == UbxAckMessageKind::kNak &&
                 record.target_class_id == 0x06u &&
                 record.target_message_id == 0x8Bu,
             "ACK-NAK should decode the rejected class/id payload");
}

void TestMalformedOrWrongFrames(TestContext& ctx)
{
  ctx.Expect(universal_gnss_protocols::ParseUbxAck(
                 BuildUbxFrame(0x01u, 0x03u, {0x06u, 0x8Au})).status ==
                 ParserStatus::kSkipped,
             "non-ACK UBX frames should be skipped by the ACK parser");

  ctx.Expect(universal_gnss_protocols::ParseUbxAck(
                 BuildUbxFrame(0x05u, 0x02u, {0x06u, 0x8Au})).status ==
                 ParserStatus::kSkipped,
             "unknown ACK-class message ids should be skipped");

  ctx.Expect(universal_gnss_protocols::ParseUbxAck(
                 BuildUbxFrame(0x05u, 0x01u, {0x06u})).status ==
                 ParserStatus::kInvalidData,
             "ACK payloads shorter than two bytes should be rejected");

  ctx.Expect(universal_gnss_protocols::ParseUbxAck(
                 BuildUbxFrame(0x05u, 0x00u, {0x06u, 0x8Au, 0x01u})).status ==
                 ParserStatus::kInvalidData,
             "ACK payloads longer than two bytes should be rejected");
}

void TestChecksumBehaviorThroughFramer(TestContext& ctx)
{
  const auto invalid_frame = BuildUbxFrame(0x05u, 0x01u, {0x06u, 0x8Au}, false, 2222);
  ctx.Expect(invalid_frame.checksum_status == ChecksumStatus::kInvalid,
             "framed ACK bytes with a corrupted checksum should be marked invalid");
  ctx.Expect(universal_gnss_protocols::ParseUbxAck(invalid_frame).status ==
                 ParserStatus::kInvalidData,
             "ACK parsing should reject frames whose UBX checksum is invalid");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValidAckAckParsing(ctx);
  TestValidAckNakParsing(ctx);
  TestMalformedOrWrongFrames(ctx);
  TestChecksumBehaviorThroughFramer(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols UBX ACK parser tests passed\n";
  return EXIT_SUCCESS;
}
