#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"

namespace
{

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ComputeUnicoreBinaryCrc32;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UnicoreBinaryFrame;
using universal_gnss_protocols::UnicoreBinaryFrameFramer;
using universal_gnss_protocols::kUnicoreBinaryCrcSize;
using universal_gnss_protocols::kUnicoreBinaryHeaderSize;
using universal_gnss_protocols::kUnicoreBinarySync1;
using universal_gnss_protocols::kUnicoreBinarySync2;
using universal_gnss_protocols::kUnicoreBinarySync3;

struct TestContext
{
  int failures{0};

  void Expect(bool condition, const char* message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

void AppendLittleEndian16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void AppendLittleEndian32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

std::vector<std::uint8_t> BuildBinaryFrame(
    std::uint16_t message_id,
    const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> frame;
  frame.reserve(kUnicoreBinaryHeaderSize + payload.size() + kUnicoreBinaryCrcSize);
  frame.push_back(kUnicoreBinarySync1);
  frame.push_back(kUnicoreBinarySync2);
  frame.push_back(kUnicoreBinarySync3);
  frame.push_back(64u);
  AppendLittleEndian16(frame, message_id);
  AppendLittleEndian16(frame, static_cast<std::uint16_t>(payload.size()));
  frame.push_back(0u);
  frame.push_back(1u);
  AppendLittleEndian16(frame, 2345u);
  AppendLittleEndian32(frame, 345678u);
  AppendLittleEndian32(frame, 0x00010004u);
  frame.push_back(0u);
  frame.push_back(18u);
  AppendLittleEndian16(frame, 25u);
  frame.insert(frame.end(), payload.begin(), payload.end());

  const std::uint32_t crc = ComputeUnicoreBinaryCrc32(frame.data(), frame.size());
  AppendLittleEndian32(frame, crc);
  return frame;
}

std::vector<std::uint8_t> BuildCorruptLengthCandidate(
    const std::vector<std::uint8_t>& truncated_payload_prefix,
    const std::vector<std::uint8_t>& following_frame)
{
  const std::size_t declared_payload_size =
      truncated_payload_prefix.size() + following_frame.size();
  std::vector<std::uint8_t> frame = BuildBinaryFrame(9999u, {0x42u});
  frame[6] = static_cast<std::uint8_t>(declared_payload_size & 0xFFu);
  frame[7] = static_cast<std::uint8_t>((declared_payload_size >> 8u) & 0xFFu);
  frame.resize(kUnicoreBinaryHeaderSize);
  frame.insert(frame.end(), truncated_payload_prefix.begin(), truncated_payload_prefix.end());
  frame.insert(frame.end(), following_frame.begin(), following_frame.end());
  AppendLittleEndian32(frame, ComputeUnicoreBinaryCrc32(frame.data(), frame.size()) ^ 0x01u);
  return frame;
}

ParserStatus FeedBytes(
    UnicoreBinaryFrameFramer& framer,
    const std::vector<std::uint8_t>& bytes,
    std::optional<UnicoreBinaryFrame>& record,
    std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  ParserStatus last_status = ParserStatus::kIdle;
  record.reset();
  for (const std::uint8_t byte : bytes)
  {
    const auto result = framer.PushByte(byte, timestamp_ns);
    last_status = result.status;
    if (result.record.has_value())
    {
      record = result.record;
    }
  }
  return last_status;
}

std::vector<UnicoreBinaryFrame> CollectRecords(
    UnicoreBinaryFrameFramer& framer,
    const std::vector<std::uint8_t>& bytes)
{
  std::vector<UnicoreBinaryFrame> records;
  for (const std::uint8_t byte : bytes)
  {
    const auto result = framer.PushByte(byte);
    if (result.record.has_value())
    {
      records.push_back(*result.record);
    }
  }
  return records;
}

void TestValidFrame(TestContext& ctx)
{
  UnicoreBinaryFrameFramer framer;
  const auto frame = BuildBinaryFrame(37u, {0x10u, 0x20u, 0x30u, 0x40u});
  std::optional<UnicoreBinaryFrame> record;
  const ParserStatus status = FeedBytes(framer, frame, record, 12345);

  ctx.Expect(status == ParserStatus::kRecordReady, "valid frame should emit a record");
  ctx.Expect(record.has_value(), "valid frame should produce a record");
  ctx.Expect(record->timestamp_ns.has_value() && *record->timestamp_ns == 12345,
             "valid frame should keep the first-byte timestamp");
  ctx.Expect(record->message_id == 37u, "valid frame should expose the message id");
  ctx.Expect(record->payload_length == 4u, "valid frame should expose payload length");
  ctx.Expect(record->payload_offset == kUnicoreBinaryHeaderSize,
             "valid frame should expose payload offset");
  ctx.Expect(record->week_number == 2345u, "valid frame should decode the week number");
  ctx.Expect(record->milliseconds_of_week == 345678u,
             "valid frame should decode milliseconds of week");
  ctx.Expect(record->checksum_status == ChecksumStatus::kValid,
             "valid frame should report a valid CRC");
  ctx.Expect(record->payload.size() == 4u && record->payload[2] == 0x30u,
             "valid frame should expose the payload bytes");
}

void TestPartialStreamHandling(TestContext& ctx)
{
  UnicoreBinaryFrameFramer framer;
  const auto frame = BuildBinaryFrame(2125u, {0xAAu, 0xBBu, 0xCCu});

  std::optional<UnicoreBinaryFrame> record;
  const ParserStatus partial =
      FeedBytes(framer,
                std::vector<std::uint8_t>(frame.begin(), frame.end() - 2),
                record);
  ctx.Expect(partial == ParserStatus::kNeedMoreData,
             "partial binary frame should wait for more data");
  ctx.Expect(!record.has_value(), "partial binary frame should not emit a record");

  const ParserStatus completed =
      FeedBytes(framer,
                std::vector<std::uint8_t>(frame.end() - 2, frame.end()),
                record);
  ctx.Expect(completed == ParserStatus::kRecordReady,
             "remaining bytes should complete the binary frame");
  ctx.Expect(record.has_value() && record->message_id == 2125u,
             "completed binary frame should still decode the message id");
}

void TestSyncRecoveryAfterNoise(TestContext& ctx)
{
  UnicoreBinaryFrameFramer framer;
  std::vector<std::uint8_t> bytes = {0x00u, 0x11u, kUnicoreBinarySync1, 0x00u};
  const auto frame = BuildBinaryFrame(520u, {0x01u, 0x02u});
  bytes.insert(bytes.end(), frame.begin(), frame.end());

  std::optional<UnicoreBinaryFrame> record;
  const ParserStatus status = FeedBytes(framer, bytes, record);
  ctx.Expect(status == ParserStatus::kRecordReady,
             "framer should recover after binary noise");
  ctx.Expect(record.has_value() && record->message_id == 520u,
             "sync recovery should still emit the valid frame");
}

void TestInvalidChecksumRejectedAndUnknownIdAccepted(TestContext& ctx)
{
  UnicoreBinaryFrameFramer framer;
  auto invalid = BuildBinaryFrame(9999u, {0x05u, 0x06u, 0x07u});
  invalid.back() ^= 0xFFu;

  std::optional<UnicoreBinaryFrame> record;
  const ParserStatus invalid_status = FeedBytes(framer, invalid, record);
  ctx.Expect(invalid_status == ParserStatus::kInvalidData,
             "bad binary CRC should be rejected");
  ctx.Expect(!record.has_value(), "bad binary CRC should not emit a record");

  const auto unknown_valid = BuildBinaryFrame(9999u, {0x05u, 0x06u, 0x07u});
  const ParserStatus valid_status = FeedBytes(framer, unknown_valid, record);
  ctx.Expect(valid_status == ParserStatus::kRecordReady,
             "unknown message ids should still emit when frame integrity is valid");
  ctx.Expect(record.has_value() && record->message_id == 9999u,
             "unknown message ids should be preserved");
}

void TestCorruptLengthRecoversFollowingValidFrame(TestContext& ctx)
{
  const auto following = BuildBinaryFrame(
      520u,
      {0x01u, kUnicoreBinarySync1, kUnicoreBinarySync2, kUnicoreBinarySync3, 0x02u});
  const auto corrupt = BuildCorruptLengthCandidate({}, following);

  UnicoreBinaryFrameFramer framer;
  const auto records = CollectRecords(framer, corrupt);
  ctx.Expect(records.size() == 1u,
             "corrupt N4 declared length must not swallow the following valid frame");
  if (records.size() == 1u)
  {
    ctx.Expect(records.front().message_id == 520u &&
                   records.front().checksum_status == ChecksumStatus::kValid,
               "N4 recovery must emit the embedded valid frame exactly once");
  }

  const auto truncated_corrupt = BuildCorruptLengthCandidate({0x42u}, following);
  UnicoreBinaryFrameFramer truncated_framer;
  const auto after_truncated = CollectRecords(truncated_framer, truncated_corrupt);
  ctx.Expect(after_truncated.size() == 1u && after_truncated.front().message_id == 520u &&
                 after_truncated.front().checksum_status == ChecksumStatus::kValid,
             "truncated corrupt N4 candidate must recover its following valid frame");

  UnicoreBinaryFrameFramer fragmented_framer;
  std::vector<UnicoreBinaryFrame> fragmented_records;
  for (std::size_t index = 0u; index < corrupt.size(); ++index)
  {
    const auto result = fragmented_framer.PushByte(corrupt[index]);
    if (result.record.has_value())
    {
      fragmented_records.push_back(*result.record);
    }
  }
  ctx.Expect(fragmented_records.size() == 1u &&
                 fragmented_records.front().checksum_status == ChecksumStatus::kValid,
             "N4 recovery must survive arbitrary one-byte fragmentation");

  auto bad_crc = BuildBinaryFrame(9999u, {0x10u, 0x20u});
  bad_crc.back() ^= 0x01u;
  bad_crc.insert(bad_crc.end(), following.begin(), following.end());
  UnicoreBinaryFrameFramer bad_crc_framer;
  const auto after_bad_crc = CollectRecords(bad_crc_framer, bad_crc);
  const auto valid_count = std::count_if(
      after_bad_crc.begin(), after_bad_crc.end(), [](const auto& frame)
      {
        return frame.checksum_status == ChecksumStatus::kValid;
      });
  ctx.Expect(valid_count == 1u,
             "N4 bad CRC followed by valid frame must preserve exactly one valid frame");

  UnicoreBinaryFrameFramer valid_payload_framer;
  const auto valid_payload = BuildBinaryFrame(
      520u,
      {0x10u, kUnicoreBinarySync1, kUnicoreBinarySync2, kUnicoreBinarySync3, 0x20u});
  const auto valid_payload_records = CollectRecords(valid_payload_framer, valid_payload);
  ctx.Expect(valid_payload_records.size() == 1u &&
                 valid_payload_records.front().message_id == 520u &&
                 valid_payload_records.front().checksum_status == ChecksumStatus::kValid,
             "N4 sync bytes inside a valid payload must not cause resynchronization");
}

void TestTruncatedFinalize(TestContext& ctx)
{
  UnicoreBinaryFrameFramer framer;
  const auto frame = BuildBinaryFrame(12u, {0xDEu, 0xADu, 0xBEu, 0xEFu});
  std::optional<UnicoreBinaryFrame> record;
  FeedBytes(
      framer,
      std::vector<std::uint8_t>(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(20)),
      record);

  const auto result = framer.Finalize();
  ctx.Expect(result.status == ParserStatus::kTruncated,
             "finalize should report truncated binary frames");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValidFrame(ctx);
  TestPartialStreamHandling(ctx);
  TestSyncRecoveryAfterNoise(ctx);
  TestInvalidChecksumRejectedAndUnknownIdAccepted(ctx);
  TestCorruptLengthRecoversFollowingValidFrame(ctx);
  TestTruncatedFinalize(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All Unicore binary framing tests passed\n";
  return EXIT_SUCCESS;
}
