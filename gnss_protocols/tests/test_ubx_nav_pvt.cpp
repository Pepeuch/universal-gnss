#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/ubx_parser.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRuntimeState;
using universal_gnss::HasCapability;
using universal_gnss::HasValueAvailable;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UbxCarrierSolutionStatus;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_protocols::UbxNavPvtFixType;

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

bool NearlyEqual(double lhs, double rhs, double tolerance = 1e-6)
{
  return std::fabs(lhs - rhs) <= tolerance;
}

void WriteLeU2(std::vector<std::uint8_t>& payload, std::size_t offset, std::uint16_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

void WriteLeU4(std::vector<std::uint8_t>& payload, std::size_t offset, std::uint32_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  payload[offset + 2u] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  payload[offset + 3u] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void WriteLeI4(std::vector<std::uint8_t>& payload, std::size_t offset, std::int32_t value)
{
  WriteLeU4(payload, offset, static_cast<std::uint32_t>(value));
}

UbxFrame BuildUbxFrame(std::uint8_t class_id,
                       std::uint8_t message_id,
                       const std::vector<std::uint8_t>& payload,
                       std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  std::vector<std::uint8_t> bytes;
  bytes.reserve(6u + payload.size() + 2u);
  bytes.push_back(0xB5u);
  bytes.push_back(0x62u);
  bytes.push_back(class_id);
  bytes.push_back(message_id);
  bytes.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((payload.size() >> 8) & 0xFFu));
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

std::vector<std::uint8_t> MakeNavPvtPayload()
{
  std::vector<std::uint8_t> payload(92u, 0u);

  WriteLeU4(payload, 0u, 345000u);
  WriteLeU2(payload, 4u, 2025u);
  payload[6u] = 5u;
  payload[7u] = 28u;
  payload[8u] = 12u;
  payload[9u] = 34u;
  payload[10u] = 56u;
  payload[11u] = 0x07u;
  WriteLeI4(payload, 16u, 123456789);

  payload[20u] = static_cast<std::uint8_t>(UbxNavPvtFixType::k3D);
  payload[21u] = 0x01u;
  payload[22u] = 0x00u;
  payload[23u] = 18u;

  WriteLeI4(payload, 24u, 231234567);
  WriteLeI4(payload, 28u, 485678901);
  WriteLeI4(payload, 32u, 123450);
  WriteLeI4(payload, 36u, 120000);
  WriteLeU4(payload, 40u, 250u);
  WriteLeU4(payload, 44u, 500u);
  WriteLeI4(payload, 48u, 1000);
  WriteLeI4(payload, 52u, -2000);
  WriteLeI4(payload, 56u, 300);
  WriteLeI4(payload, 60u, 2500);
  WriteLeI4(payload, 64u, 9000000);
  WriteLeU4(payload, 72u, 5000u);
  WriteLeI4(payload, 84u, 12345678);

  return payload;
}

void TestValid3dFixParsing(TestContext& ctx)
{
  const UbxFrame frame = BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload(), 1111);
  const auto result = universal_gnss_protocols::ParseUbxNavPvt(frame);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid NAV-PVT frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(1111),
             "NAV-PVT should preserve the framing timestamp");
  ctx.Expect(record.i_tow_ms == 345000u, "NAV-PVT should decode iTOW");
  ctx.Expect(record.valid_date && record.valid_time && record.fully_resolved_time,
             "NAV-PVT should decode date/time validity bits");
  ctx.Expect(record.fix_type == UbxNavPvtFixType::k3D && record.gnss_fix_ok,
             "NAV-PVT should decode fixType and gnssFixOK");
  ctx.Expect(record.carrier_solution == UbxCarrierSolutionStatus::kNone,
             "NAV-PVT should decode no carrier solution");
  ctx.Expect(record.num_sv == 18u, "NAV-PVT should decode numSV");
  ctx.Expect(NearlyEqual(record.longitude_deg, 23.1234567) &&
                 NearlyEqual(record.latitude_deg, 48.5678901),
             "NAV-PVT should convert lon/lat from 1e-7 degrees");
  ctx.Expect(NearlyEqual(record.height_ellipsoid_m, 123.45) &&
                 NearlyEqual(record.height_msl_m, 120.0),
             "NAV-PVT should convert heights from millimeters");
  ctx.Expect(NearlyEqual(record.horizontal_accuracy_m, 0.25) &&
                 NearlyEqual(record.vertical_accuracy_m, 0.5),
             "NAV-PVT should convert accuracy from millimeters");
}

void TestRtkFloatAndFixedMapping(TestContext& ctx)
{
  auto float_payload = MakeNavPvtPayload();
  float_payload[21u] = static_cast<std::uint8_t>(0x01u | (1u << 6));
  const auto float_record =
      universal_gnss_protocols::ParseUbxNavPvt(BuildUbxFrame(0x01u, 0x07u, float_payload)).record;
  ctx.Expect(float_record.has_value(), "RTK float test requires a parsed NAV-PVT record");
  if (!float_record.has_value())
  {
    return;
  }

  const GnssRuntimeState float_state = universal_gnss_protocols::UbxNavPvtToRuntimeState(*float_record);
  ctx.Expect(float_state.rtk_mode == std::optional<universal_gnss::GnssRtkMode>(
                                     universal_gnss::GnssRtkMode::kFloat),
             "carrier solution 1 should map to RTK float");

  auto fixed_payload = MakeNavPvtPayload();
  fixed_payload[21u] = static_cast<std::uint8_t>(0x01u | (2u << 6));
  const auto fixed_record =
      universal_gnss_protocols::ParseUbxNavPvt(BuildUbxFrame(0x01u, 0x07u, fixed_payload)).record;
  ctx.Expect(fixed_record.has_value(), "RTK fixed test requires a parsed NAV-PVT record");
  if (!fixed_record.has_value())
  {
    return;
  }

  const GnssRuntimeState fixed_state = universal_gnss_protocols::UbxNavPvtToRuntimeState(*fixed_record);
  ctx.Expect(fixed_state.rtk_mode == std::optional<universal_gnss::GnssRtkMode>(
                                     universal_gnss::GnssRtkMode::kFixed),
             "carrier solution 2 should map to RTK fixed");
  ctx.Expect(fixed_state.fix_type == GnssFixType::kFix,
             "carrier solution should not replace the generic fix type mapping");
}

void TestNoFixMapping(TestContext& ctx)
{
  auto payload = MakeNavPvtPayload();
  payload[20u] = static_cast<std::uint8_t>(UbxNavPvtFixType::kNoFix);
  payload[21u] = 0x00u;
  payload[23u] = 0u;
  const auto result = universal_gnss_protocols::ParseUbxNavPvt(BuildUbxFrame(0x01u, 0x07u, payload));

  ctx.Expect(result.record.has_value(), "no-fix test requires a parsed NAV-PVT record");
  if (!result.record.has_value())
  {
    return;
  }

  const GnssRuntimeState state = universal_gnss_protocols::UbxNavPvtToRuntimeState(*result.record);
  ctx.Expect(!state.fix_valid && state.fix_type == GnssFixType::kNoFix,
             "fixType 0 should map to no-fix");
  ctx.Expect(!state.latitude_deg.has_value() && !state.altitude_m.has_value(),
             "no-fix mapping should not invent valid position values");
  ctx.Expect(state.rtk_mode == std::optional<universal_gnss::GnssRtkMode>(
                                   universal_gnss::GnssRtkMode::kNone),
             "no carrier solution should map to explicit RTK none");
}

void TestMalformedOrWrongFrames(TestContext& ctx)
{
  const UbxFrame wrong_message = BuildUbxFrame(0x01u, 0x35u, std::vector<std::uint8_t>(8u, 0u));
  ctx.Expect(universal_gnss_protocols::ParseUbxNavPvt(wrong_message).status == ParserStatus::kSkipped,
             "wrong UBX class/id should be skipped");

  UbxFrame short_payload = BuildUbxFrame(0x01u, 0x07u, std::vector<std::uint8_t>(91u, 0u));
  ctx.Expect(universal_gnss_protocols::ParseUbxNavPvt(short_payload).status ==
                 ParserStatus::kInvalidData,
             "wrong NAV-PVT payload length should be rejected");

  UbxFrame invalid_checksum = BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload());
  invalid_checksum.checksum_status = ChecksumStatus::kInvalid;
  ctx.Expect(universal_gnss_protocols::ParseUbxNavPvt(invalid_checksum).status ==
                 ParserStatus::kInvalidData,
             "invalid checksum status should be rejected");
}

void TestHeadingAndAccuracyRuntimeMapping(TestContext& ctx)
{
  auto payload = MakeNavPvtPayload();
  payload[21u] = static_cast<std::uint8_t>(0x01u | (1u << 5));
  const auto result = universal_gnss_protocols::ParseUbxNavPvt(BuildUbxFrame(0x01u, 0x07u, payload, 2222));

  ctx.Expect(result.record.has_value(), "runtime mapping test requires a parsed NAV-PVT record");
  if (!result.record.has_value())
  {
    return;
  }

  const GnssRuntimeState state = universal_gnss_protocols::UbxNavPvtToRuntimeState(*result.record);
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(2222),
             "runtime mapping should preserve the framing timestamp");
  ctx.Expect(state.fix_valid && state.fix_type == GnssFixType::kFix,
             "3D NAV-PVT should map to a generic valid fix");
  ctx.Expect(state.latitude_deg.has_value() && NearlyEqual(*state.latitude_deg, 48.5678901) &&
                 state.longitude_deg.has_value() && NearlyEqual(*state.longitude_deg, 23.1234567),
             "runtime mapping should expose coordinates");
  ctx.Expect(state.altitude_m.has_value() && NearlyEqual(*state.altitude_m, 120.0),
             "runtime mapping should prefer hMSL for altitude");
  ctx.Expect(HasCapability(state, GnssCapability::kRtkMode) &&
                 HasCapability(state, GnssCapability::kHorizontalAccuracy) &&
                 HasCapability(state, GnssCapability::kVerticalAccuracy) &&
                 HasCapability(state, GnssCapability::kSatellitesUsed),
             "runtime mapping should advertise supported NAV-PVT optional fields");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kRtkMode) &&
                 HasValueAvailable(state, GnssCapability::kHorizontalAccuracy) &&
                 HasValueAvailable(state, GnssCapability::kVerticalAccuracy) &&
                 HasValueAvailable(state, GnssCapability::kSatellitesUsed),
             "runtime mapping should expose present NAV-PVT optional values");
  ctx.Expect(state.horizontal_accuracy_m == std::optional<float>(0.25f) &&
                 state.vertical_accuracy_m == std::optional<float>(0.5f) &&
                 state.satellites_used == std::optional<std::uint16_t>(18u),
             "runtime mapping should convert accuracy and numSV");
  ctx.Expect(HasCapability(state, GnssCapability::kHeading) &&
                 HasValueAvailable(state, GnssCapability::kHeading) &&
                 state.heading_deg.has_value() && NearlyEqual(*state.heading_deg, 123.45678),
             "runtime mapping should expose heading only when headVehValid is set");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValid3dFixParsing(ctx);
  TestRtkFloatAndFixedMapping(ctx);
  TestNoFixMapping(ctx);
  TestMalformedOrWrongFrames(ctx);
  TestHeadingAndAccuracyRuntimeMapping(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols UBX NAV-PVT tests passed\n";
  return EXIT_SUCCESS;
}
