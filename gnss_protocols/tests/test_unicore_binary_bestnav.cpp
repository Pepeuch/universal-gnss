#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/unicore_parser.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::HasCapability;
using universal_gnss::HasValueAvailable;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ComputeUnicoreBinaryCrc32;
using universal_gnss_protocols::ParseUnicoreBestNavB;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UnicoreBestNavBToRuntimeState;
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

  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

bool NearlyEqual(const double lhs, const double rhs, const double tolerance = 1e-6)
{
  return std::fabs(lhs - rhs) <= tolerance;
}

void AppendLittleEndian16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void AppendLittleEndian32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void WriteLittleEndian32(std::vector<std::uint8_t>& bytes,
                         const std::size_t offset,
                         const std::uint32_t value)
{
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void WriteLittleEndianFloat32(std::vector<std::uint8_t>& bytes,
                              const std::size_t offset,
                              const float value)
{
  std::uint32_t raw = 0u;
  std::memcpy(&raw, &value, sizeof(raw));
  WriteLittleEndian32(bytes, offset, raw);
}

void WriteLittleEndianFloat64(std::vector<std::uint8_t>& bytes,
                              const std::size_t offset,
                              const double value)
{
  std::uint64_t raw = 0u;
  std::memcpy(&raw, &value, sizeof(raw));
  bytes[offset] = static_cast<std::uint8_t>(raw & 0xFFu);
  bytes[offset + 1u] = static_cast<std::uint8_t>((raw >> 8) & 0xFFu);
  bytes[offset + 2u] = static_cast<std::uint8_t>((raw >> 16) & 0xFFu);
  bytes[offset + 3u] = static_cast<std::uint8_t>((raw >> 24) & 0xFFu);
  bytes[offset + 4u] = static_cast<std::uint8_t>((raw >> 32) & 0xFFu);
  bytes[offset + 5u] = static_cast<std::uint8_t>((raw >> 40) & 0xFFu);
  bytes[offset + 6u] = static_cast<std::uint8_t>((raw >> 48) & 0xFFu);
  bytes[offset + 7u] = static_cast<std::uint8_t>((raw >> 56) & 0xFFu);
}

UnicoreBinaryFrame BuildBinaryFrame(const std::uint16_t message_id,
                                    const std::vector<std::uint8_t>& payload,
                                    const std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  std::vector<std::uint8_t> frame;
  frame.reserve(kUnicoreBinaryHeaderSize + payload.size() + kUnicoreBinaryCrcSize);
  frame.push_back(kUnicoreBinarySync1);
  frame.push_back(kUnicoreBinarySync2);
  frame.push_back(kUnicoreBinarySync3);
  frame.push_back(97u);
  AppendLittleEndian16(frame, message_id);
  AppendLittleEndian16(frame, static_cast<std::uint16_t>(payload.size()));
  frame.push_back(0u);
  frame.push_back(1u);
  AppendLittleEndian16(frame, 2294u);
  AppendLittleEndian32(frame, 472312000u);
  AppendLittleEndian32(frame, 18u);
  frame.push_back(0u);
  frame.push_back(16u);
  AppendLittleEndian16(frame, 0u);
  frame.insert(frame.end(), payload.begin(), payload.end());

  const std::uint32_t crc = ComputeUnicoreBinaryCrc32(frame.data(), frame.size());
  AppendLittleEndian32(frame, crc);

  UnicoreBinaryFrameFramer framer;
  universal_gnss_protocols::ParserResult<UnicoreBinaryFrame> result;
  for (const auto byte : frame)
  {
    result = framer.PushByte(byte, timestamp_ns);
  }

  if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
  {
    std::cerr << "FAILED: could not frame BESTNAVB test frame\n";
    std::exit(EXIT_FAILURE);
  }

  return *result.record;
}

std::vector<std::uint8_t> MakeBestNavPayload(const std::uint32_t solution_status_code,
                                             const std::uint32_t position_type_code)
{
  std::vector<std::uint8_t> payload(120u, 0u);

  WriteLittleEndian32(payload, 0u, solution_status_code);
  WriteLittleEndian32(payload, 4u, position_type_code);
  WriteLittleEndianFloat64(payload, 8u, 40.0789588272);
  WriteLittleEndianFloat64(payload, 16u, 116.2365102982);
  WriteLittleEndianFloat64(payload, 24u, 65.8312);
  WriteLittleEndianFloat32(payload, 32u, -8.4925f);
  WriteLittleEndian32(payload, 36u, 61u);
  WriteLittleEndianFloat32(payload, 40u, 1.2221f);
  WriteLittleEndianFloat32(payload, 44u, 1.1053f);
  WriteLittleEndianFloat32(payload, 48u, 2.1970f);
  payload[52u] = '0';
  WriteLittleEndianFloat32(payload, 56u, 0.4f);
  WriteLittleEndianFloat32(payload, 60u, 0.2f);
  payload[64u] = 50u;
  payload[65u] = 28u;
  return payload;
}

void TestValidBestNavBParseAndMapping(TestContext& ctx)
{
  const UnicoreBinaryFrame frame =
      BuildBinaryFrame(2118u, MakeBestNavPayload(0u, 34u), 123456);
  ctx.Expect(frame.checksum_status == ChecksumStatus::kValid,
             "BESTNAVB test frame should have a valid CRC");

  const auto result = ParseUnicoreBestNavB(frame);
  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid BESTNAVB frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.header.timestamp_ns == std::optional<std::int64_t>(123456) &&
                 record.header.message_id == 2118u &&
                 record.header.payload_length == 120u,
             "BESTNAVB should preserve binary header metadata");
  ctx.Expect(record.solution_status ==
                 universal_gnss_protocols::UnicoreSolutionStatus::kSolComputed &&
                 record.position_type ==
                     universal_gnss_protocols::UnicorePositionType::kNarrowFloat,
             "BESTNAVB should decode documented solution and position types");
  ctx.Expect(NearlyEqual(record.latitude_deg, 40.0789588272) &&
                 NearlyEqual(record.longitude_deg, 116.2365102982) &&
                 NearlyEqual(record.altitude_m, 65.8312),
             "BESTNAVB should decode latitude, longitude, and altitude");
  ctx.Expect(record.datum_is_wgs84.has_value() && *record.datum_is_wgs84 &&
                 record.diff_age_s == 0.4f &&
                 record.tracked_satellites == 50u &&
                 record.used_satellites == 28u,
             "BESTNAVB should decode documented age and satellite counters");

  const auto state = UnicoreBestNavBToRuntimeState(record);
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFloat &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFloat),
             "BESTNAVB runtime mapping should expose RTK float from NARROW_FLOAT");
  ctx.Expect(HasCapability(state, GnssCapability::kHorizontalAccuracy) &&
                 HasCapability(state, GnssCapability::kVerticalAccuracy) &&
                 HasCapability(state, GnssCapability::kCorrectionAge) &&
                 HasCapability(state, GnssCapability::kSatellitesTracked) &&
                 HasCapability(state, GnssCapability::kSatellitesUsed),
             "BESTNAVB runtime mapping should advertise documented optional fields");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kHorizontalAccuracy) &&
                 HasValueAvailable(state, GnssCapability::kVerticalAccuracy) &&
                 HasValueAvailable(state, GnssCapability::kCorrectionAge) &&
                 state.correction_age_s == 0.4f,
             "BESTNAVB runtime mapping should expose documented accuracy and correction age");
  ctx.Expect(!HasCapability(state, GnssCapability::kHeading) &&
                 !HasCapability(state, GnssCapability::kInterferenceState) &&
                 !HasCapability(state, GnssCapability::kJammingState),
             "BESTNAVB should not invent heading or RF runtime fields");
}

void TestFixedMappingAndNoHeadingInference(TestContext& ctx)
{
  const auto result = ParseUnicoreBestNavB(
      BuildBinaryFrame(2118u, MakeBestNavPayload(0u, 50u), 9876));
  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "second BESTNAVB frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto state = UnicoreBestNavBToRuntimeState(*result.record);
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFixed &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed),
             "BESTNAVB should expose RTK fixed from NARROW_INT");
  ctx.Expect(!HasCapability(state, GnssCapability::kHeading),
             "BESTNAVB should not create heading capabilities");
}

void TestWrongIdAndMalformedPayloadRejected(TestContext& ctx)
{
  const auto wrong_id_result =
      ParseUnicoreBestNavB(BuildBinaryFrame(999u, MakeBestNavPayload(0u, 34u)));
  ctx.Expect(wrong_id_result.status == ParserStatus::kInvalidData,
             "wrong binary message id should be rejected");

  std::vector<std::uint8_t> truncated_payload(64u, 0u);
  WriteLittleEndian32(truncated_payload, 0u, 0u);
  WriteLittleEndian32(truncated_payload, 4u, 34u);
  const auto truncated_result = ParseUnicoreBestNavB(BuildBinaryFrame(2118u, truncated_payload));
  ctx.Expect(truncated_result.status == ParserStatus::kInvalidData,
             "truncated BESTNAVB payload should be rejected");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValidBestNavBParseAndMapping(ctx);
  TestFixedMappingAndNoHeadingInference(ctx);
  TestWrongIdAndMalformedPayloadRejected(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All Unicore BESTNAVB tests passed\n";
  return EXIT_SUCCESS;
}
