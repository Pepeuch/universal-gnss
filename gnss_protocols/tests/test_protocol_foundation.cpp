#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/unicore_framer.hpp"

namespace
{

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaSentenceFramer;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::RtcmFrameFramer;
using universal_gnss_protocols::UbxChecksum;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_protocols::UnicoreFrameFramer;

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

template <typename FramerT, typename RecordT>
universal_gnss_protocols::ParserResult<RecordT> FeedBytes(
    FramerT& framer,
    const std::vector<std::uint8_t>& bytes,
    std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  universal_gnss_protocols::ParserResult<RecordT> result;
  for (const auto byte : bytes)
  {
    result = framer.PushByte(byte, timestamp_ns);
  }
  return result;
}

template <typename FramerT, typename RecordT>
std::vector<RecordT> CollectRecords(FramerT& framer,
                                    const std::vector<std::uint8_t>& bytes,
                                    std::vector<ParserStatus>* statuses = nullptr)
{
  std::vector<RecordT> records;
  for (const auto byte : bytes)
  {
    const auto result = framer.PushByte(byte);
    if (statuses != nullptr)
    {
      statuses->push_back(result.status);
    }
    if (result.record.has_value())
    {
      records.push_back(*result.record);
    }
  }
  return records;
}

std::vector<std::uint8_t> BuildUbxFrame(const std::uint8_t class_id,
                                        const std::uint8_t message_id,
                                        const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> frame = {
      0xB5u,
      0x62u,
      class_id,
      message_id,
      static_cast<std::uint8_t>(payload.size() & 0xFFu),
      static_cast<std::uint8_t>((payload.size() >> 8u) & 0xFFu),
  };
  frame.insert(frame.end(), payload.begin(), payload.end());
  const auto checksum = universal_gnss_protocols::ComputeUbxChecksum(
      frame.data() + 2u, frame.size() - 2u);
  frame.push_back(checksum.ck_a);
  frame.push_back(checksum.ck_b);
  return frame;
}

std::vector<std::uint8_t> BuildRtcmFrame(const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> frame = {
      0xD3u,
      static_cast<std::uint8_t>((payload.size() >> 8u) & 0x03u),
      static_cast<std::uint8_t>(payload.size() & 0xFFu),
  };
  frame.insert(frame.end(), payload.begin(), payload.end());
  const auto crc = universal_gnss_protocols::ComputeRtcmCrc24Q(frame.data(), frame.size());
  frame.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return frame;
}

std::vector<std::uint8_t> BuildUbxCorruptLengthCandidate(
    const std::vector<std::uint8_t>& truncated_payload_prefix,
    const std::vector<std::uint8_t>& following_frame)
{
  const std::size_t declared_payload_size =
      truncated_payload_prefix.size() + following_frame.size();
  std::vector<std::uint8_t> frame = BuildUbxFrame(0x06u, 0x01u, {0x42u});
  frame[4] = static_cast<std::uint8_t>(declared_payload_size & 0xFFu);
  frame[5] = static_cast<std::uint8_t>((declared_payload_size >> 8u) & 0xFFu);
  frame.resize(6u);
  frame.insert(frame.end(), truncated_payload_prefix.begin(), truncated_payload_prefix.end());
  frame.insert(frame.end(), following_frame.begin(), following_frame.end());
  const auto checksum = universal_gnss_protocols::ComputeUbxChecksum(
      frame.data() + 2u, frame.size() - 2u);
  frame.push_back(static_cast<std::uint8_t>(checksum.ck_a ^ 0x01u));
  frame.push_back(checksum.ck_b);
  return frame;
}

std::vector<std::uint8_t> BuildRtcmCorruptLengthCandidate(
    const std::vector<std::uint8_t>& truncated_payload_prefix,
    const std::vector<std::uint8_t>& following_frame)
{
  const std::size_t declared_payload_size =
      truncated_payload_prefix.size() + following_frame.size();
  std::vector<std::uint8_t> frame = BuildRtcmFrame({0x42u});
  frame[1] = static_cast<std::uint8_t>((declared_payload_size >> 8u) & 0x03u);
  frame[2] = static_cast<std::uint8_t>(declared_payload_size & 0xFFu);
  frame.resize(3u);
  frame.insert(frame.end(), truncated_payload_prefix.begin(), truncated_payload_prefix.end());
  frame.insert(frame.end(), following_frame.begin(), following_frame.end());
  const auto crc = universal_gnss_protocols::ComputeRtcmCrc24Q(frame.data(), frame.size());
  frame.push_back(static_cast<std::uint8_t>(((crc >> 16u) & 0xFFu) ^ 0x01u));
  frame.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return frame;
}

std::vector<std::uint8_t> ToBytes(const std::string& text)
{
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string WithNmeaChecksum(const char leader, const std::string& payload)
{
  const std::uint8_t checksum = universal_gnss_protocols::ComputeNmeaChecksum(payload);
  std::ostringstream stream;
  stream << leader << payload << '*' << std::uppercase << std::hex << std::setw(2)
         << std::setfill('0') << static_cast<unsigned int>(checksum) << "\r\n";
  return stream.str();
}

std::string WithUnicoreAsciiCrc(const std::string& frame_without_crc)
{
  if (frame_without_crc.empty() || frame_without_crc.front() != '#')
  {
    std::cerr << "FAILED: invalid Unicore ASCII test vector\n";
    std::exit(EXIT_FAILURE);
  }

  const auto crc = universal_gnss_protocols::ComputeUnicoreBinaryCrc32(
      reinterpret_cast<const std::uint8_t*>(frame_without_crc.data() + 1u),
      frame_without_crc.size() - 1u);

  std::ostringstream stream;
  stream << frame_without_crc
         << '*'
         << std::hex
         << std::nouppercase
         << std::setw(8)
         << std::setfill('0')
         << crc
         << "\r\n";
  return stream.str();
}

void TestNmeaChecksumHelpers(TestContext& ctx)
{
  const std::string frame = "$GPGLL,4916.45,N,12311.12,W,225444,A,*1D\r\n";
  std::optional<std::uint8_t> reported;
  std::optional<std::uint8_t> computed;
  const auto status =
      universal_gnss_protocols::ValidateNmeaChecksum(frame, &reported, &computed);

  ctx.Expect(status == ChecksumStatus::kValid, "known NMEA vector should validate");
  ctx.Expect(reported.has_value() && *reported == 0x1Du,
             "known NMEA vector should expose the reported checksum");
  ctx.Expect(computed.has_value() && *computed == 0x1Du,
             "known NMEA vector should expose the computed checksum");

  const auto invalid_status =
      universal_gnss_protocols::ValidateNmeaChecksum("$GPGLL,4916.45,N*ZZ\r\n");
  ctx.Expect(invalid_status == ChecksumStatus::kInvalid,
             "malformed NMEA checksum text should be rejected");
}

void TestRtcmCrc24QHelpers(TestContext& ctx)
{
  const std::string payload = "123456789";
  const auto crc = universal_gnss_protocols::ComputeRtcmCrc24Q(
      reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
  ctx.Expect(crc == 0x00CDE703u, "CRC24Q known vector should match 0xCDE703");
  ctx.Expect(universal_gnss_protocols::ValidateRtcmCrc24Q(
                 reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), crc),
             "CRC24Q validation should accept the known vector");
  ctx.Expect(!universal_gnss_protocols::ValidateRtcmCrc24Q(
                 reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(),
                 crc ^ 0x1u),
             "CRC24Q validation should reject a modified checksum");
}

void TestUbxChecksumHelpers(TestContext& ctx)
{
  const std::uint8_t message[] = {0x01u, 0x07u, 0x00u, 0x00u};
  const UbxChecksum checksum =
      universal_gnss_protocols::ComputeUbxChecksum(message, sizeof(message));

  ctx.Expect(checksum.ck_a == 0x08u && checksum.ck_b == 0x19u,
             "UBX Fletcher checksum known vector should match 0x08 0x19");
  ctx.Expect(universal_gnss_protocols::ValidateUbxChecksum(message, sizeof(message), checksum),
             "UBX checksum validation should accept the known vector");
  ctx.Expect(!universal_gnss_protocols::ValidateUbxChecksum(
                 message, sizeof(message), UbxChecksum{checksum.ck_a, static_cast<std::uint8_t>(
                                                                          checksum.ck_b + 1u)}),
             "UBX checksum validation should reject a modified checksum");
}

void TestNmeaFramerPartialAndTruncatedHandling(TestContext& ctx)
{
  NmeaSentenceFramer framer;
  const auto partial = FeedBytes<NmeaSentenceFramer, universal_gnss_protocols::NmeaSentence>(
      framer, ToBytes("$GPGLL,4916.45,N,12311.12,W,225444,A,*1D\r"));
  ctx.Expect(partial.status == ParserStatus::kNeedMoreData,
             "NMEA framer should wait for LF while the frame is partial");

  const auto ready = framer.PushByte('\n', 1234);
  ctx.Expect(ready.status == ParserStatus::kRecordReady && ready.record.has_value(),
             "NMEA framer should emit a record once LF arrives");
  ctx.Expect(ready.record->talker == "GP", "NMEA framer should extract the talker");
  ctx.Expect(ready.record->sentence_type == "GLL",
             "NMEA framer should extract the sentence type");
  ctx.Expect(ready.record->checksum_status == ChecksumStatus::kValid,
             "NMEA framer should validate the sentence checksum");

  FeedBytes<NmeaSentenceFramer, universal_gnss_protocols::NmeaSentence>(
      framer, ToBytes("$GPRMC,1"));
  const auto truncated = framer.Finalize();
  ctx.Expect(truncated.status == ParserStatus::kTruncated,
             "NMEA framer should report truncated data on finalize");
}

void TestNmeaFramerHeaderExtractionOwnsViewedStorage(TestContext& ctx)
{
  const std::string long_header = "GPLONGPROPRIETARYHEADER_" + std::string(80u, 'X');
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,10.0,M,46.9,M,,", "GGA"},
      {long_header + ",payload", long_header.substr(2u)},
  };

  for (const auto& [payload, expected_sentence_type] : cases)
  {
    NmeaSentenceFramer framer;
    const auto result = FeedBytes<NmeaSentenceFramer, universal_gnss_protocols::NmeaSentence>(
        framer, ToBytes(WithNmeaChecksum('$', payload)), 4321);
    ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
               "NMEA framer should emit each header-lifetime regression vector");
    if (!result.record.has_value())
    {
      continue;
    }

    ctx.Expect(result.record->talker == "GP",
               "NMEA framer should preserve talker extraction for short and long headers");
    ctx.Expect(result.record->sentence_type == expected_sentence_type,
               "NMEA framer should preserve sentence extraction for short and long headers");
    ctx.Expect(result.record->checksum_status == ChecksumStatus::kValid,
               "NMEA framer should preserve checksum handling for header-lifetime vectors");
  }
}

void TestNmeaFramerResynchronizesOnNestedLeader(TestContext& ctx)
{
  struct ResynchronizationCase
  {
    std::string truncated_candidate;
    std::string intervening_garbage;
    char following_leader;
  };

  const std::vector<ResynchronizationCase> cases = {
      {"$GPGGA,123519,4807.038,N", "", '$'},
      {"$GPGGA,123519,4807.038,N", "", '!'},
      {"!GPGGA,123519,4807.038,N", "", '$'},
      {"$GPGGA,123519,4807.038,N", "garbage", '$'},
  };
  constexpr std::int64_t kTruncatedTimestampNs = 1000;
  constexpr std::int64_t kFollowingLeaderTimestampNs = 2000;
  constexpr std::int64_t kFollowingPayloadTimestampNs = 3000;

  for (const auto& test_case : cases)
  {
    NmeaSentenceFramer framer;
    FeedBytes<NmeaSentenceFramer, universal_gnss_protocols::NmeaSentence>(
        framer, ToBytes(test_case.truncated_candidate), kTruncatedTimestampNs);
    FeedBytes<NmeaSentenceFramer, universal_gnss_protocols::NmeaSentence>(
        framer, ToBytes(test_case.intervening_garbage), kTruncatedTimestampNs);

    const std::string following_sentence = WithNmeaChecksum(
        test_case.following_leader, "GPGLL,4916.45,N,12311.12,W,225444,A,");
    std::vector<universal_gnss_protocols::NmeaSentence> records;
    for (std::size_t index = 0u; index < following_sentence.size(); ++index)
    {
      const auto result = framer.PushByte(
          static_cast<std::uint8_t>(following_sentence[index]),
          index == 0u ? std::optional<std::int64_t>(kFollowingLeaderTimestampNs)
                      : std::optional<std::int64_t>(kFollowingPayloadTimestampNs));
      if (result.status == ParserStatus::kRecordReady && result.record.has_value())
      {
        records.push_back(*result.record);
      }
    }

    ctx.Expect(records.size() == 1u,
               "NMEA framer should emit exactly the following valid sentence after a nested leader");
    if (records.size() != 1u)
    {
      continue;
    }

    const auto& record = records.front();
    ctx.Expect(record.leader == test_case.following_leader && record.talker == "GP" &&
                   record.sentence_type == "GLL" &&
                   record.checksum_status == ChecksumStatus::kValid &&
                   record.timestamp_ns == std::optional<std::int64_t>(kFollowingLeaderTimestampNs),
               "NMEA framer should resynchronize at a nested leader without losing the new frame or its timestamp");
  }
}

void TestRtcmFramerBoundaryAndSyncRecovery(TestContext& ctx)
{
  RtcmFrameFramer framer;
  const std::vector<std::uint8_t> frame = {0x00u, 0xD3u, 0x00u, 0x02u, 0x3Eu, 0xD0u, 0xA4u, 0xE0u,
                                           0x00u};
  auto result =
      FeedBytes<RtcmFrameFramer, universal_gnss_protocols::RtcmFrame>(framer, frame, 222);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "RTCM framer should recover after noise and emit a record");
  ctx.Expect(result.record->message_type == 1005u,
             "RTCM framer should extract the 12-bit message type");
  ctx.Expect(result.record->checksum_status == ChecksumStatus::kValid,
             "RTCM framer should validate the frame CRC");

  const std::vector<std::uint8_t> invalid_header = {0xD3u, 0xFFu, 0xD3u, 0x00u, 0x02u, 0x3Eu,
                                                    0xD0u, 0xA4u, 0xE0u, 0x00u};
  result = FeedBytes<RtcmFrameFramer, universal_gnss_protocols::RtcmFrame>(
      framer, invalid_header);
  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "RTCM framer should recover from an invalid header and resync on a new preamble");

  FeedBytes<RtcmFrameFramer, universal_gnss_protocols::RtcmFrame>(
      framer, {0xD3u, 0x00u, 0x02u, 0x3Eu});
  const auto truncated = framer.Finalize();
  ctx.Expect(truncated.status == ParserStatus::kTruncated,
             "RTCM framer should report truncated data on finalize");
}

void TestUbxFramerPartialHandlingAndChecksum(TestContext& ctx)
{
  UbxFrameFramer framer;
  const std::vector<std::uint8_t> frame = {0x00u, 0xB5u, 0x00u, 0xB5u, 0x62u, 0x01u,
                                           0x07u, 0x00u, 0x00u, 0x08u, 0x19u};
  const auto result =
      FeedBytes<UbxFrameFramer, universal_gnss_protocols::UbxFrame>(framer, frame, 555);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "UBX framer should recover sync and emit a frame");
  ctx.Expect(result.record->class_id == 0x01u && result.record->message_id == 0x07u,
             "UBX framer should expose class and message ids");
  ctx.Expect(result.record->checksum_status == ChecksumStatus::kValid,
             "UBX framer should validate the checksum");

  FeedBytes<UbxFrameFramer, universal_gnss_protocols::UbxFrame>(
      framer, {0xB5u, 0x62u, 0x01u, 0x07u});
  const auto truncated = framer.Finalize();
  ctx.Expect(truncated.status == ParserStatus::kTruncated,
             "UBX framer should report truncated data on finalize");
}

void TestUbxFramerRecoversFollowingValidFrameAfterCorruptLength(TestContext& ctx)
{
  const auto following = BuildUbxFrame(0x01u, 0x07u, {0x10u, 0xB5u, 0x62u, 0x20u});
  const auto corrupt = BuildUbxCorruptLengthCandidate({}, following);

  UbxFrameFramer framer;
  const auto records = CollectRecords<UbxFrameFramer, universal_gnss_protocols::UbxFrame>(
      framer, corrupt);
  ctx.Expect(records.size() == 1u,
             "UBX corrupt declared length must not swallow the following valid frame");
  if (records.size() == 1u)
  {
    ctx.Expect(records.front().class_id == 0x01u && records.front().message_id == 0x07u &&
                   records.front().checksum_status == ChecksumStatus::kValid,
               "UBX recovery must emit the embedded valid frame exactly once");
  }

  const auto truncated_corrupt = BuildUbxCorruptLengthCandidate({0x42u}, following);
  UbxFrameFramer truncated_framer;
  const auto after_truncated =
      CollectRecords<UbxFrameFramer, universal_gnss_protocols::UbxFrame>(
          truncated_framer, truncated_corrupt);
  ctx.Expect(after_truncated.size() == 1u &&
                 after_truncated.front().class_id == 0x01u &&
                 after_truncated.front().message_id == 0x07u &&
                 after_truncated.front().checksum_status == ChecksumStatus::kValid,
             "UBX truncated corrupt candidate must recover its following valid frame");

  UbxFrameFramer fragmented_framer;
  std::vector<universal_gnss_protocols::UbxFrame> fragmented_records;
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
             "UBX recovery must survive arbitrary one-byte fragmentation");

  auto bad_checksum = BuildUbxFrame(0x01u, 0x02u, {0x33u});
  bad_checksum.back() ^= 0x01u;
  bad_checksum.insert(bad_checksum.end(), following.begin(), following.end());
  UbxFrameFramer bad_checksum_framer;
  const auto after_bad_checksum =
      CollectRecords<UbxFrameFramer, universal_gnss_protocols::UbxFrame>(
          bad_checksum_framer, bad_checksum);
  const auto valid_count = std::count_if(
      after_bad_checksum.begin(), after_bad_checksum.end(), [](const auto& frame)
      {
        return frame.checksum_status == ChecksumStatus::kValid;
      });
  ctx.Expect(valid_count == 1u,
             "UBX bad checksum followed by valid frame must preserve exactly one valid frame");

  UbxFrameFramer valid_payload_framer;
  const auto valid_payload = BuildUbxFrame(0x01u, 0x30u, {0x11u, 0xB5u, 0x62u, 0x22u});
  const auto valid_payload_records =
      CollectRecords<UbxFrameFramer, universal_gnss_protocols::UbxFrame>(
          valid_payload_framer, valid_payload);
  ctx.Expect(valid_payload_records.size() == 1u &&
                 valid_payload_records.front().class_id == 0x01u &&
                 valid_payload_records.front().message_id == 0x30u &&
                 valid_payload_records.front().checksum_status == ChecksumStatus::kValid,
             "UBX sync bytes inside a valid payload must not cause resynchronization");
}

void TestRtcmFramerRecoversFollowingValidFrameAfterCorruptLength(TestContext& ctx)
{
  const auto following = BuildRtcmFrame({0x3Eu, 0xD0u, 0xD3u, 0x55u});
  const auto corrupt = BuildRtcmCorruptLengthCandidate({}, following);

  RtcmFrameFramer framer;
  const auto records = CollectRecords<RtcmFrameFramer, universal_gnss_protocols::RtcmFrame>(
      framer, corrupt);
  ctx.Expect(records.size() == 1u,
             "RTCM corrupt declared length must not swallow the following valid frame");
  if (records.size() == 1u)
  {
    ctx.Expect(records.front().message_type == 1005u &&
                   records.front().checksum_status == ChecksumStatus::kValid,
               "RTCM recovery must emit the embedded valid frame exactly once");
  }

  const auto truncated_corrupt = BuildRtcmCorruptLengthCandidate({0x42u}, following);
  RtcmFrameFramer truncated_framer;
  const auto after_truncated =
      CollectRecords<RtcmFrameFramer, universal_gnss_protocols::RtcmFrame>(
          truncated_framer, truncated_corrupt);
  ctx.Expect(after_truncated.size() == 1u &&
                 after_truncated.front().message_type == 1005u &&
                 after_truncated.front().checksum_status == ChecksumStatus::kValid,
             "RTCM truncated corrupt candidate must recover its following valid frame");

  RtcmFrameFramer fragmented_framer;
  std::vector<universal_gnss_protocols::RtcmFrame> fragmented_records;
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
             "RTCM recovery must survive arbitrary one-byte fragmentation");

  auto bad_crc = BuildRtcmFrame({0x3Eu, 0xD0u});
  bad_crc.back() ^= 0x01u;
  bad_crc.insert(bad_crc.end(), following.begin(), following.end());
  RtcmFrameFramer bad_crc_framer;
  const auto after_bad_crc = CollectRecords<RtcmFrameFramer, universal_gnss_protocols::RtcmFrame>(
      bad_crc_framer, bad_crc);
  const auto valid_count = std::count_if(
      after_bad_crc.begin(), after_bad_crc.end(), [](const auto& frame)
      {
        return frame.checksum_status == ChecksumStatus::kValid;
      });
  ctx.Expect(valid_count == 1u,
             "RTCM bad CRC followed by valid frame must preserve exactly one valid frame");

  RtcmFrameFramer valid_payload_framer;
  const auto valid_payload = BuildRtcmFrame({0x3Eu, 0xD0u, 0xD3u, 0x55u});
  const auto valid_payload_records =
      CollectRecords<RtcmFrameFramer, universal_gnss_protocols::RtcmFrame>(
          valid_payload_framer, valid_payload);
  ctx.Expect(valid_payload_records.size() == 1u &&
                 valid_payload_records.front().message_type == 1005u &&
                 valid_payload_records.front().checksum_status == ChecksumStatus::kValid,
             "RTCM preamble bytes inside a valid payload must not cause resynchronization");
}

void TestUnicoreFramerSyncRecovery(TestContext& ctx)
{
  UnicoreFrameFramer framer;
  const std::string ascii_frame = WithUnicoreAsciiCrc("#BESTPOSA,1,2,3");
  const auto result =
      FeedBytes<UnicoreFrameFramer, universal_gnss_protocols::UnicoreFrame>(
          framer, ToBytes("noise" + ascii_frame), 999);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "Unicore framer should recover after noise and emit a frame");
  ctx.Expect(result.record->message_name == "BESTPOSA",
             "Unicore framer should extract the message name");
  ctx.Expect(result.record->checksum_status == ChecksumStatus::kValid,
             "Unicore framer should validate the documented ASCII CRC");
  ctx.Expect(result.record->reported_crc32 == result.record->computed_crc32,
             "Unicore framer should expose matching reported and computed CRC32 values");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestNmeaChecksumHelpers(ctx);
  TestRtcmCrc24QHelpers(ctx);
  TestUbxChecksumHelpers(ctx);
  TestNmeaFramerPartialAndTruncatedHandling(ctx);
  TestNmeaFramerHeaderExtractionOwnsViewedStorage(ctx);
  TestNmeaFramerResynchronizesOnNestedLeader(ctx);
  TestRtcmFramerBoundaryAndSyncRecovery(ctx);
  TestUbxFramerPartialHandlingAndChecksum(ctx);
  TestUbxFramerRecoversFollowingValidFrameAfterCorruptLength(ctx);
  TestRtcmFramerRecoversFollowingValidFrameAfterCorruptLength(ctx);
  TestUnicoreFramerSyncRecovery(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols foundation tests passed\n";
  return EXIT_SUCCESS;
}
