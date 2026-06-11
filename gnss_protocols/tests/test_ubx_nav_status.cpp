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
using universal_gnss::GnssRtkMode;
using universal_gnss::GnssRuntimeState;
using universal_gnss::HasCapability;
using universal_gnss::HasValueAvailable;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UbxCarrierSolutionStatus;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_protocols::UbxNavStatusFixType;

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

void WriteLeU4(std::vector<std::uint8_t>& payload, std::size_t offset, std::uint32_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  payload[offset + 2u] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  payload[offset + 3u] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
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

std::vector<std::uint8_t> MakeNavStatusPayload()
{
  std::vector<std::uint8_t> payload(16u, 0u);
  WriteLeU4(payload, 0u, 456789u);
  payload[4u] = static_cast<std::uint8_t>(UbxNavStatusFixType::kNoFix);
  payload[5u] = 0x00u;
  payload[6u] = 0x00u;
  payload[7u] = 0x00u;
  WriteLeU4(payload, 8u, 1500u);
  WriteLeU4(payload, 12u, 42000u);
  return payload;
}

void TestValidNoFixParsing(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxNavStatus(
      BuildUbxFrame(0x01u, 0x03u, MakeNavStatusPayload(), 1234));

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid NAV-STATUS no-fix frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(1234),
             "NAV-STATUS should preserve the framing timestamp");
  ctx.Expect(record.i_tow_ms == 456789u, "NAV-STATUS should decode iTOW");
  ctx.Expect(record.gps_fix == UbxNavStatusFixType::kNoFix,
             "NAV-STATUS should decode gpsFix");
  ctx.Expect(!record.gnss_fix_ok && !record.differential_solution,
             "NAV-STATUS should decode no-fix status flags");
  ctx.Expect(!record.carrier_solution_valid &&
                 record.carrier_solution == UbxCarrierSolutionStatus::kNone,
             "NAV-STATUS should decode missing carrier solution");
  ctx.Expect(record.ttff_ms == 1500u && record.msss_ms == 42000u,
             "NAV-STATUS should decode ttff and msss");
}

void TestValidFixAndCarrierSolutionParsing(TestContext& ctx)
{
  auto payload = MakeNavStatusPayload();
  payload[4u] = static_cast<std::uint8_t>(UbxNavStatusFixType::k3D);
  payload[5u] = static_cast<std::uint8_t>((1u << 0) | (1u << 1));
  payload[6u] = static_cast<std::uint8_t>(1u << 1);
  payload[7u] = static_cast<std::uint8_t>(1u << 6);

  const auto result =
      universal_gnss_protocols::ParseUbxNavStatus(BuildUbxFrame(0x01u, 0x03u, payload));

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid NAV-STATUS 3D frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.gps_fix == UbxNavStatusFixType::k3D && record.gnss_fix_ok,
             "NAV-STATUS should decode a valid 3D fix");
  ctx.Expect(record.differential_solution,
             "NAV-STATUS should decode the diffSoln bit");
  ctx.Expect(record.carrier_solution_valid &&
                 record.carrier_solution == UbxCarrierSolutionStatus::kFloat,
             "NAV-STATUS should decode a valid float carrier solution");
}

void TestMalformedOrWrongFrames(TestContext& ctx)
{
  const UbxFrame wrong_message =
      BuildUbxFrame(0x01u, 0x07u, std::vector<std::uint8_t>(16u, 0u));
  ctx.Expect(universal_gnss_protocols::ParseUbxNavStatus(wrong_message).status ==
                 ParserStatus::kSkipped,
             "wrong UBX class/id should be skipped");

  const UbxFrame short_payload =
      BuildUbxFrame(0x01u, 0x03u, std::vector<std::uint8_t>(15u, 0u));
  ctx.Expect(universal_gnss_protocols::ParseUbxNavStatus(short_payload).status ==
                 ParserStatus::kInvalidData,
             "wrong NAV-STATUS payload length should be rejected");

  UbxFrame invalid_checksum = BuildUbxFrame(0x01u, 0x03u, MakeNavStatusPayload());
  invalid_checksum.checksum_status = ChecksumStatus::kInvalid;
  ctx.Expect(universal_gnss_protocols::ParseUbxNavStatus(invalid_checksum).status ==
                 ParserStatus::kInvalidData,
             "invalid checksum status should be rejected");
}

void TestRuntimeMappingBehavior(TestContext& ctx)
{
  auto no_carrier_payload = MakeNavStatusPayload();
  no_carrier_payload[4u] = static_cast<std::uint8_t>(UbxNavStatusFixType::k2D);
  no_carrier_payload[5u] = static_cast<std::uint8_t>((1u << 0) | (1u << 1));
  const auto no_carrier_record = universal_gnss_protocols::ParseUbxNavStatus(
      BuildUbxFrame(0x01u, 0x03u, no_carrier_payload, 888));
  ctx.Expect(no_carrier_record.record.has_value(),
             "runtime mapping test requires a parsed NAV-STATUS record");
  if (!no_carrier_record.record.has_value())
  {
    return;
  }

  const GnssRuntimeState no_carrier_state =
      universal_gnss_protocols::UbxNavStatusToRuntimeState(*no_carrier_record.record);
  ctx.Expect(no_carrier_state.timestamp_ns == std::optional<std::int64_t>(888),
             "runtime mapping should preserve the framing timestamp");
  ctx.Expect(no_carrier_state.fix_valid && no_carrier_state.fix_type == GnssFixType::kFix,
             "gpsFix=2D with gpsFixOk should map to a generic valid fix");
  ctx.Expect(HasCapability(no_carrier_state, GnssCapability::kRtkMode) &&
                 !HasValueAvailable(no_carrier_state, GnssCapability::kRtkMode) &&
                 !no_carrier_state.rtk_mode.has_value(),
             "missing carrSoln validity should leave RTK mode supported but unknown");
  ctx.Expect(HasValueAvailable(no_carrier_state, GnssCapability::kDifferentialCorrections) &&
                 HasValueAvailable(no_carrier_state, GnssCapability::kCorrectionsActive) &&
                 no_carrier_state.differential_corrections == std::optional<bool>(true) &&
                 no_carrier_state.corrections_active == std::optional<bool>(true),
             "explicit diffSoln should expose known true correction state even without carrSoln");
  ctx.Expect(!HasCapability(no_carrier_state, GnssCapability::kHorizontalAccuracy) &&
                 !HasCapability(no_carrier_state, GnssCapability::kSatellitesUsed) &&
                 !HasCapability(no_carrier_state, GnssCapability::kInterferenceState),
             "NAV-STATUS should not invent accuracy, satellite, or RF capabilities");

  auto fixed_payload = MakeNavStatusPayload();
  fixed_payload[4u] = static_cast<std::uint8_t>(UbxNavStatusFixType::k3D);
  fixed_payload[5u] = static_cast<std::uint8_t>((1u << 0) | (1u << 1));
  fixed_payload[6u] = static_cast<std::uint8_t>(1u << 1);
  fixed_payload[7u] = static_cast<std::uint8_t>(2u << 6);
  const auto fixed_record = universal_gnss_protocols::ParseUbxNavStatus(
      BuildUbxFrame(0x01u, 0x03u, fixed_payload));
  ctx.Expect(fixed_record.record.has_value(),
             "fixed RTK mapping test requires a parsed NAV-STATUS record");
  if (!fixed_record.record.has_value())
  {
    return;
  }

  const GnssRuntimeState fixed_state =
      universal_gnss_protocols::UbxNavStatusToRuntimeState(*fixed_record.record);
  ctx.Expect(fixed_state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed) &&
                 HasValueAvailable(fixed_state, GnssCapability::kRtkMode),
             "valid carrier solution 2 should map to RTK fixed");
  ctx.Expect(fixed_state.differential_corrections == std::optional<bool>(true) &&
                 fixed_state.corrections_active == std::optional<bool>(true),
             "diffSoln should mark the solution as known corrected and active");
  ctx.Expect(!fixed_state.latitude_deg.has_value() && !fixed_state.horizontal_accuracy_m.has_value(),
             "NAV-STATUS should not invent position or accuracy values");
  ctx.Expect(!HasCapability(fixed_state, GnssCapability::kMeanCn0) &&
                 !HasCapability(fixed_state, GnssCapability::kJammingState),
             "NAV-STATUS should not invent CN0 or RF capabilities");

  auto dead_reckoning_payload = MakeNavStatusPayload();
  dead_reckoning_payload[4u] = static_cast<std::uint8_t>(UbxNavStatusFixType::kDeadReckoningOnly);
  const auto dead_reckoning_record = universal_gnss_protocols::ParseUbxNavStatus(
      BuildUbxFrame(0x01u, 0x03u, dead_reckoning_payload));
  ctx.Expect(dead_reckoning_record.record.has_value(),
             "dead reckoning mapping test requires a parsed NAV-STATUS record");
  if (!dead_reckoning_record.record.has_value())
  {
    return;
  }

  const GnssRuntimeState dead_reckoning_state =
      universal_gnss_protocols::UbxNavStatusToRuntimeState(*dead_reckoning_record.record);
  ctx.Expect(!dead_reckoning_state.fix_valid &&
                 dead_reckoning_state.fix_type == GnssFixType::kDeadReckoning,
             "gpsFix=dead reckoning should map conservatively");
  ctx.Expect(dead_reckoning_state.differential_corrections == std::optional<bool>(false) &&
                 dead_reckoning_state.corrections_active == std::optional<bool>(false),
             "cleared diffSoln should expose known false correction state");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValidNoFixParsing(ctx);
  TestValidFixAndCarrierSolutionParsing(ctx);
  TestMalformedOrWrongFrames(ctx);
  TestRuntimeMappingBehavior(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols UBX NAV-STATUS tests passed\n";
  return EXIT_SUCCESS;
}
