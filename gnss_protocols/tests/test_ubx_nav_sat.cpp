#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/ubx_parser.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssRuntimeState;
using universal_gnss::HasCapability;
using universal_gnss::HasValueAvailable;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;

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

void WriteLeI2(std::vector<std::uint8_t>& payload, std::size_t offset, std::int16_t value)
{
  WriteLeU2(payload, offset, static_cast<std::uint16_t>(value));
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

std::vector<std::uint8_t> MakeNavSatPayload()
{
  std::vector<std::uint8_t> payload(8u + (3u * 12u), 0u);
  WriteLeU4(payload, 0u, 456000u);
  payload[4u] = 0x01u;
  payload[5u] = 3u;

  // SV 0: used, healthy, cno 45
  payload[8u] = 0u;
  payload[9u] = 4u;
  payload[10u] = 45u;
  payload[11u] = static_cast<std::uint8_t>(30);
  WriteLeI2(payload, 12u, 120);
  WriteLeI2(payload, 14u, 0);
  WriteLeU4(payload, 16u, 0x0000001Cu);

  // SV 1: not used, unknown health, cno 38
  payload[20u] = 2u;
  payload[21u] = 12u;
  payload[22u] = 38u;
  payload[23u] = static_cast<std::uint8_t>(15);
  WriteLeI2(payload, 24u, 220);
  WriteLeI2(payload, 26u, 0);
  WriteLeU4(payload, 28u, 0x00000004u);

  // SV 2: used, unhealthy, no signal cno 0
  payload[32u] = 0u;
  payload[33u] = 18u;
  payload[34u] = 0u;
  payload[35u] = static_cast<std::uint8_t>(5);
  WriteLeI2(payload, 36u, 300);
  WriteLeI2(payload, 38u, 0);
  WriteLeU4(payload, 40u, 0x0000002Cu);

  return payload;
}

void TestValidNavSatParsing(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxNavSat(
      BuildUbxFrame(0x01u, 0x35u, MakeNavSatPayload(), 777));

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid NAV-SAT frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(777),
             "NAV-SAT should preserve the framing timestamp");
  ctx.Expect(record.i_tow_ms == 456000u, "NAV-SAT should decode iTOW");
  ctx.Expect(record.version == 0x01u && record.num_svs == 3u,
             "NAV-SAT should decode version and numSvs");
  ctx.Expect(record.satellite_count == 3u, "NAV-SAT should decode all repeated blocks");
  ctx.Expect(record.used_satellite_count == 2u, "NAV-SAT should count used satellites");
  ctx.Expect(record.satellites[0].gnss_id == 0u &&
                 record.satellites[0].sv_id == 4u &&
                 record.satellites[0].cno_db_hz == 45u &&
                 record.satellites[0].used_in_navigation &&
                 record.satellites[0].healthy == std::optional<bool>(true),
             "NAV-SAT should decode the first satellite block");
  ctx.Expect(record.satellites[2].healthy == std::optional<bool>(false),
             "NAV-SAT should decode unhealthy satellites");
}

void TestMalformedOrWrongFrames(TestContext& ctx)
{
  const UbxFrame wrong_message = BuildUbxFrame(0x01u, 0x07u, std::vector<std::uint8_t>(92u, 0u));
  ctx.Expect(universal_gnss_protocols::ParseUbxNavSat(wrong_message).status == ParserStatus::kSkipped,
             "wrong UBX class/id should be skipped");

  auto short_payload = MakeNavSatPayload();
  short_payload.pop_back();
  ctx.Expect(universal_gnss_protocols::ParseUbxNavSat(BuildUbxFrame(0x01u, 0x35u, short_payload)).status ==
                 ParserStatus::kInvalidData,
             "truncated NAV-SAT payload should be rejected");

  auto wrong_version = MakeNavSatPayload();
  wrong_version[4u] = 0x02u;
  ctx.Expect(universal_gnss_protocols::ParseUbxNavSat(BuildUbxFrame(0x01u, 0x35u, wrong_version)).status ==
                 ParserStatus::kInvalidData,
             "unsupported NAV-SAT version should be rejected");

  UbxFrame invalid_checksum = BuildUbxFrame(0x01u, 0x35u, MakeNavSatPayload());
  invalid_checksum.checksum_status = ChecksumStatus::kInvalid;
  ctx.Expect(universal_gnss_protocols::ParseUbxNavSat(invalid_checksum).status ==
                 ParserStatus::kInvalidData,
             "invalid checksum status should be rejected");
}

void TestRuntimeMappingBehavior(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxNavSat(
      BuildUbxFrame(0x01u, 0x35u, MakeNavSatPayload(), 888));
  ctx.Expect(result.record.has_value(), "runtime mapping test requires a parsed NAV-SAT record");
  if (!result.record.has_value())
  {
    return;
  }

  const GnssRuntimeState state = universal_gnss_protocols::UbxNavSatToRuntimeState(*result.record);
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(888),
             "runtime mapping should preserve the framing timestamp");
  ctx.Expect(HasCapability(state, GnssCapability::kSatellitesVisible) &&
                 HasCapability(state, GnssCapability::kSatellitesUsed) &&
                 HasCapability(state, GnssCapability::kMeanCn0) &&
                 HasCapability(state, GnssCapability::kMaxCn0),
             "runtime mapping should advertise NAV-SAT capabilities");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kSatellitesVisible) &&
                 HasValueAvailable(state, GnssCapability::kSatellitesUsed) &&
                 HasValueAvailable(state, GnssCapability::kMeanCn0) &&
                 HasValueAvailable(state, GnssCapability::kMaxCn0),
             "runtime mapping should expose present NAV-SAT values");
  ctx.Expect(state.satellites_visible == std::optional<std::uint16_t>(3u) &&
                 state.satellites_used == std::optional<std::uint16_t>(2u),
             "runtime mapping should expose visible and used satellite counts");
  ctx.Expect(state.mean_cn0_db_hz.has_value() && NearlyEqual(*state.mean_cn0_db_hz, 41.5) &&
                 state.max_cn0_db_hz.has_value() && NearlyEqual(*state.max_cn0_db_hz, 45.0),
             "runtime mapping should compute CN0 mean/max from non-zero C/N0 values");
  ctx.Expect(!HasCapability(state, GnssCapability::kRtkMode),
             "runtime mapping should not invent RTK capability");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValidNavSatParsing(ctx);
  TestMalformedOrWrongFrames(ctx);
  TestRuntimeMappingBehavior(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols UBX NAV-SAT tests passed\n";
  return EXIT_SUCCESS;
}
