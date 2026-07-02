#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
#include "universal_gnss_protocols/unicore_framer.hpp"
#include "universal_gnss_protocols/unicore_parser.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssBaselineSolutionStatus;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::HasCapability;
using universal_gnss::HasValueAvailable;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ComputeUnicoreBinaryCrc32;
using universal_gnss_protocols::ParseUnicorePvtsln;
using universal_gnss_protocols::ParseUnicorePvtslnB;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UnicoreFrame;
using universal_gnss_protocols::UnicoreFrameFramer;
using universal_gnss_protocols::UnicorePvtslnToRuntimeState;
using universal_gnss_protocols::UnicoreBinaryFrame;
using universal_gnss_protocols::UnicoreBinaryFrameFramer;
using universal_gnss_protocols::UnicorePvtslnBToRuntimeState;
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
  AppendLittleEndian16(frame, 2190u);
  AppendLittleEndian32(frame, 364536000u);
  AppendLittleEndian32(frame, 18u);
  frame.push_back(0u);
  frame.push_back(13u);
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
    std::cerr << "FAILED: could not frame PVTSLNB test frame\n";
    std::exit(EXIT_FAILURE);
  }

  return *result.record;
}

std::vector<std::uint8_t> MakePvtslnPayload(const std::uint32_t best_position_type_code,
                                            const std::uint32_t baseline_status_code)
{
  std::vector<std::uint8_t> payload(224u, 0u);

  WriteLittleEndian32(payload, 0u, best_position_type_code);
  WriteLittleEndianFloat32(payload, 4u, 60.5060f);
  WriteLittleEndianFloat64(payload, 8u, 40.07898130522);
  WriteLittleEndianFloat64(payload, 16u, 116.23663134427);
  WriteLittleEndianFloat32(payload, 24u, 0.2000f);
  WriteLittleEndianFloat32(payload, 28u, 0.1500f);
  WriteLittleEndianFloat32(payload, 32u, 0.1800f);
  WriteLittleEndianFloat32(payload, 36u, 0.9000f);

  WriteLittleEndian32(payload, 40u, 16u);
  WriteLittleEndianFloat32(payload, 44u, 60.5060f);
  WriteLittleEndianFloat64(payload, 48u, 40.07898130522);
  WriteLittleEndianFloat64(payload, 56u, 116.23663134427);

  WriteLittleEndianFloat32(payload, 64u, -8.4923f);
  payload[68u] = 46u;
  payload[69u] = 28u;
  payload[70u] = 46u;
  payload[71u] = 28u;

  WriteLittleEndianFloat64(payload, 72u, 0.0009);
  WriteLittleEndianFloat64(payload, 80u, -0.0031);
  WriteLittleEndianFloat64(payload, 88u, 0.0032);

  WriteLittleEndian32(payload, 96u, baseline_status_code);
  WriteLittleEndianFloat32(payload, 100u, 1.5000f);
  WriteLittleEndianFloat32(payload, 104u, 182.2500f);
  WriteLittleEndianFloat32(payload, 108u, 0.1000f);
  payload[112u] = 28u;
  payload[113u] = 25u;
  payload[114u] = 12u;
  payload[115u] = 8u;
  WriteLittleEndianFloat32(payload, 116u, 2.1753f);
  WriteLittleEndianFloat32(payload, 120u, 1.3480f);
  WriteLittleEndianFloat32(payload, 124u, 0.6840f);
  WriteLittleEndianFloat32(payload, 128u, 1.8392f);
  WriteLittleEndianFloat32(payload, 132u, 1.7072f);
  WriteLittleEndianFloat32(payload, 136u, 5.0f);
  return payload;
}

std::string WithUnicoreAsciiCrc(const std::string& frame_without_crc)
{
  const auto crc = ComputeUnicoreBinaryCrc32(
      reinterpret_cast<const std::uint8_t*>(frame_without_crc.data() + 1u),
      frame_without_crc.size() - 1u);

  char checksum[9] = {};
  std::snprintf(checksum, sizeof(checksum), "%08x", crc);
  return frame_without_crc + "*" + checksum + "\r\n";
}

UnicoreFrame BuildAsciiFrame(const std::string& line,
                             const std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  UnicoreFrameFramer framer;
  universal_gnss_protocols::ParserResult<UnicoreFrame> result;
  for (const char ch : line)
  {
    result = framer.PushByte(static_cast<std::uint8_t>(ch), timestamp_ns);
  }

  if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
  {
    std::cerr << "FAILED: could not frame PVTSLNA test line\n";
    std::exit(EXIT_FAILURE);
  }

  return *result.record;
}

std::string MakePvtslnAsciiLine()
{
  return WithUnicoreAsciiCrc(
      "#PVTSLNA,97,GPS,FINE,2190,364536000,0,0,18,13;"
      "NARROW_INT,60.5060,40.07898130522,116.23663134427,0.2000,0.1500,0.1800,0.9000,"
      "SINGLE,60.5060,40.07898130522,116.23663134427,-8.4923,46,28,46,28,0.0009,-0.0031,0.0032,"
      "SOL_COMPUTED,1.5000,182.2500,0.1000,28,25,12,8,2.1753,1.3480,0.6840,1.8392,1.7072,5.0,"
      "28,25,26");
}

void TestValidPvtslnBParseAndRuntimeMapping(TestContext& ctx)
{
  const UnicoreBinaryFrame frame =
      BuildBinaryFrame(1021u, MakePvtslnPayload(50u, 0u), 1111);
  ctx.Expect(frame.checksum_status == ChecksumStatus::kValid,
             "PVTSLNB test frame should have a valid CRC");

  const auto result = ParseUnicorePvtslnB(frame);
  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid PVTSLNB frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.header.timestamp_ns == std::optional<std::int64_t>(1111) &&
                 record.header.message_id == 1021u &&
                 record.header.payload_length == 224u,
             "PVTSLNB should preserve binary header metadata");
  ctx.Expect(record.best_position_type ==
                 universal_gnss_protocols::UnicorePositionType::kNarrowInt &&
                 record.baseline_solution_status ==
                     universal_gnss_protocols::UnicoreSolutionStatus::kSolComputed,
             "PVTSLNB should decode documented position and baseline solution fields");
  ctx.Expect(NearlyEqual(record.best_altitude_m, 60.5060) &&
                 NearlyEqual(record.best_latitude_deg, 40.07898130522) &&
                 NearlyEqual(record.best_longitude_deg, 116.23663134427),
             "PVTSLNB should decode best-position coordinates and altitude");
  ctx.Expect(record.best_diff_age_s == 0.9000f &&
                 record.best_tracked_satellites == 46u &&
                 record.best_used_satellites == 28u &&
                 record.hdop == 0.6840f &&
                 record.baseline_azimuth_deg == 182.2500f,
             "PVTSLNB should decode documented age, satellite, DOP, and baseline azimuth fields");

  const auto state = UnicorePvtslnBToRuntimeState(record);
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFixed &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed),
             "PVTSLNB runtime mapping should expose RTK fixed from NARROW_INT");
  ctx.Expect(HasCapability(state, GnssCapability::kHorizontalAccuracy) &&
                 HasCapability(state, GnssCapability::kVerticalAccuracy) &&
                 HasCapability(state, GnssCapability::kCorrectionAge) &&
                 HasCapability(state, GnssCapability::kHeading) &&
                 HasCapability(state, GnssCapability::kDualAntennaBaseline) &&
                 HasCapability(state, GnssCapability::kBaselineAzimuth) &&
                 HasCapability(state, GnssCapability::kBaselinePitch) &&
                 HasCapability(state, GnssCapability::kBaselineLength) &&
                 HasCapability(state, GnssCapability::kBaselineSolutionStatus) &&
                 HasCapability(state, GnssCapability::kHdop),
             "PVTSLNB runtime mapping should advertise documented optional fields");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kHorizontalAccuracy) &&
                 HasValueAvailable(state, GnssCapability::kVerticalAccuracy) &&
                 HasValueAvailable(state, GnssCapability::kCorrectionAge) &&
                 HasValueAvailable(state, GnssCapability::kHeading) &&
                 HasValueAvailable(state, GnssCapability::kDualAntennaBaseline) &&
                 HasValueAvailable(state, GnssCapability::kBaselineAzimuth) &&
                 HasValueAvailable(state, GnssCapability::kBaselinePitch) &&
                 HasValueAvailable(state, GnssCapability::kBaselineLength) &&
                 HasValueAvailable(state, GnssCapability::kBaselineSolutionStatus) &&
                 HasValueAvailable(state, GnssCapability::kHdop) &&
                 state.heading_deg == 182.2500f &&
                 state.dual_antenna_baseline == std::optional<bool>(true) &&
                 state.baseline_azimuth_deg == std::optional<float>(182.2500f) &&
                 state.baseline_pitch_deg == std::optional<float>(0.1000f) &&
                 state.baseline_length_m == std::optional<float>(1.5000f) &&
                 state.baseline_solution_status ==
                     std::optional<GnssBaselineSolutionStatus>(
                         GnssBaselineSolutionStatus::kComputed) &&
                 state.correction_age_s == 0.9000f &&
                 state.hdop == 0.6840f,
             "PVTSLNB runtime mapping should expose documented baseline, heading, DOP, and age values");
  ctx.Expect(!HasCapability(state, GnssCapability::kInterferenceState) &&
                 !HasCapability(state, GnssCapability::kJammingState),
             "PVTSLNB should not invent RF runtime fields");
}

void TestAsciiAndBinaryRuntimeConsistency(TestContext& ctx)
{
  const auto binary_result =
      ParseUnicorePvtslnB(BuildBinaryFrame(1021u, MakePvtslnPayload(50u, 0u), 4444));
  const auto ascii_result = ParseUnicorePvtsln(BuildAsciiFrame(MakePvtslnAsciiLine(), 4444));
  ctx.Expect(binary_result.status == ParserStatus::kRecordReady &&
                 binary_result.record.has_value() &&
                 ascii_result.status == ParserStatus::kRecordReady &&
                 ascii_result.record.has_value(),
             "matching PVTSLNB and PVTSLNA test vectors should both parse successfully");
  if (!binary_result.record.has_value() || !ascii_result.record.has_value())
  {
    return;
  }

  const auto binary_state = UnicorePvtslnBToRuntimeState(*binary_result.record);
  const auto ascii_state = UnicorePvtslnToRuntimeState(*ascii_result.record);
  ctx.Expect(binary_state.timestamp_ns == ascii_state.timestamp_ns &&
                 binary_state.fix_type == ascii_state.fix_type &&
                 binary_state.rtk_mode == ascii_state.rtk_mode &&
                 binary_state.dual_antenna_baseline == ascii_state.dual_antenna_baseline &&
                 binary_state.baseline_solution_status == ascii_state.baseline_solution_status,
             "ASCII and binary PVTSLN runtime mapping should agree on fix and baseline status");
  ctx.Expect(binary_state.latitude_deg.has_value() &&
                 ascii_state.latitude_deg.has_value() &&
                 NearlyEqual(*binary_state.latitude_deg, *ascii_state.latitude_deg) &&
                 binary_state.longitude_deg.has_value() &&
                 ascii_state.longitude_deg.has_value() &&
                 NearlyEqual(*binary_state.longitude_deg, *ascii_state.longitude_deg) &&
                 binary_state.altitude_m.has_value() &&
                 ascii_state.altitude_m.has_value() &&
                 NearlyEqual(*binary_state.altitude_m, *ascii_state.altitude_m, 1e-4),
             "ASCII and binary PVTSLN runtime mapping should agree on coordinates and altitude");
  ctx.Expect(binary_state.baseline_azimuth_deg == ascii_state.baseline_azimuth_deg &&
                 binary_state.baseline_pitch_deg == ascii_state.baseline_pitch_deg &&
                 binary_state.baseline_length_m == ascii_state.baseline_length_m &&
                 binary_state.heading_deg == ascii_state.heading_deg &&
                 binary_state.hdop == ascii_state.hdop &&
                 binary_state.correction_age_s == ascii_state.correction_age_s,
             "ASCII and binary PVTSLN runtime mapping should agree on shared baseline geometry, heading compatibility, HDOP, and correction age");
}

void TestHeadingIsGatedByHeadingSolutionStatus(TestContext& ctx)
{
  const auto result = ParseUnicorePvtslnB(
      BuildBinaryFrame(1021u, MakePvtslnPayload(34u, 1u), 2222));
  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "second PVTSLNB frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto state = UnicorePvtslnBToRuntimeState(*result.record);
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFloat &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFloat),
             "PVTSLNB should expose RTK float from NARROW_FLOAT");
  ctx.Expect(HasCapability(state, GnssCapability::kHeading) &&
                 !HasValueAvailable(state, GnssCapability::kHeading) &&
                 HasCapability(state, GnssCapability::kDualAntennaBaseline) &&
                 HasCapability(state, GnssCapability::kBaselineSolutionStatus) &&
                 HasValueAvailable(state, GnssCapability::kDualAntennaBaseline) &&
                 HasValueAvailable(state, GnssCapability::kBaselineSolutionStatus) &&
                 state.dual_antenna_baseline == std::optional<bool>(false) &&
                 state.baseline_solution_status ==
                     std::optional<GnssBaselineSolutionStatus>(
                         GnssBaselineSolutionStatus::kInsufficientObservations) &&
                 !HasValueAvailable(state, GnssCapability::kBaselineAzimuth),
             "PVTSLNB should gate baseline geometry while still exposing a known unsolved baseline state");
}

void TestWrongIdAndMalformedPayloadRejected(TestContext& ctx)
{
  const auto wrong_id_result =
      ParseUnicorePvtslnB(BuildBinaryFrame(999u, MakePvtslnPayload(50u, 0u)));
  ctx.Expect(wrong_id_result.status == ParserStatus::kInvalidData,
             "wrong binary message id should be rejected");

  std::vector<std::uint8_t> truncated_payload(160u, 0u);
  WriteLittleEndian32(truncated_payload, 0u, 50u);
  const auto truncated_result = ParseUnicorePvtslnB(BuildBinaryFrame(1021u, truncated_payload));
  ctx.Expect(truncated_result.status == ParserStatus::kInvalidData,
             "truncated PVTSLNB payload should be rejected");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValidPvtslnBParseAndRuntimeMapping(ctx);
  TestAsciiAndBinaryRuntimeConsistency(ctx);
  TestHeadingIsGatedByHeadingSolutionStatus(ctx);
  TestWrongIdAndMalformedPayloadRejected(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All Unicore PVTSLNB tests passed\n";
  return EXIT_SUCCESS;
}
