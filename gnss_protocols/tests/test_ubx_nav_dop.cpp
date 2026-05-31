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

bool NearlyEqual(const double lhs, const double rhs, const double tolerance = 1e-6)
{
  return std::fabs(lhs - rhs) <= tolerance;
}

void WriteLeU2(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::uint16_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

void WriteLeU4(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::uint32_t value)
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

std::vector<std::uint8_t> MakeNavDopPayload()
{
  std::vector<std::uint8_t> payload(18u, 0u);
  WriteLeU4(payload, 0u, 567890u);
  WriteLeU2(payload, 4u, 145u);
  WriteLeU2(payload, 6u, 123u);
  WriteLeU2(payload, 8u, 99u);
  WriteLeU2(payload, 10u, 87u);
  WriteLeU2(payload, 12u, 65u);
  WriteLeU2(payload, 14u, 111u);
  WriteLeU2(payload, 16u, 109u);
  return payload;
}

void TestValidNavDopParsing(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxNavDop(
      BuildUbxFrame(0x01u, 0x04u, MakeNavDopPayload(), 999));

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid NAV-DOP frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(999),
             "NAV-DOP should preserve the framing timestamp");
  ctx.Expect(record.i_tow_ms == 567890u, "NAV-DOP should decode iTOW");
  ctx.Expect(NearlyEqual(record.g_dop, 1.45) &&
                 NearlyEqual(record.p_dop, 1.23) &&
                 NearlyEqual(record.t_dop, 0.99) &&
                 NearlyEqual(record.v_dop, 0.87) &&
                 NearlyEqual(record.h_dop, 0.65) &&
                 NearlyEqual(record.n_dop, 1.11) &&
                 NearlyEqual(record.e_dop, 1.09),
             "NAV-DOP should scale all DOP values from 0.01 units");
}

void TestMalformedOrWrongFrames(TestContext& ctx)
{
  const UbxFrame wrong_message =
      BuildUbxFrame(0x01u, 0x07u, std::vector<std::uint8_t>(18u, 0u));
  ctx.Expect(universal_gnss_protocols::ParseUbxNavDop(wrong_message).status ==
                 ParserStatus::kSkipped,
             "wrong UBX class/id should be skipped");

  const UbxFrame short_payload =
      BuildUbxFrame(0x01u, 0x04u, std::vector<std::uint8_t>(17u, 0u));
  ctx.Expect(universal_gnss_protocols::ParseUbxNavDop(short_payload).status ==
                 ParserStatus::kInvalidData,
             "wrong NAV-DOP payload length should be rejected");

  UbxFrame invalid_checksum = BuildUbxFrame(0x01u, 0x04u, MakeNavDopPayload());
  invalid_checksum.checksum_status = ChecksumStatus::kInvalid;
  ctx.Expect(universal_gnss_protocols::ParseUbxNavDop(invalid_checksum).status ==
                 ParserStatus::kInvalidData,
             "invalid checksum status should be rejected");
}

void TestRuntimeMappingBehavior(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxNavDop(
      BuildUbxFrame(0x01u, 0x04u, MakeNavDopPayload(), 12345));
  ctx.Expect(result.record.has_value(), "runtime mapping test requires a parsed NAV-DOP record");
  if (!result.record.has_value())
  {
    return;
  }

  const GnssRuntimeState state = universal_gnss_protocols::UbxNavDopToRuntimeState(*result.record);
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(12345),
             "runtime mapping should preserve the framing timestamp");
  ctx.Expect(HasCapability(state, GnssCapability::kHdop) &&
                 HasCapability(state, GnssCapability::kVdop),
             "runtime mapping should advertise NAV-DOP DOP capabilities");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kHdop) &&
                 HasValueAvailable(state, GnssCapability::kVdop),
             "runtime mapping should expose present NAV-DOP values");
  ctx.Expect(state.hdop.has_value() && NearlyEqual(*state.hdop, 0.65) &&
                 state.vdop.has_value() && NearlyEqual(*state.vdop, 0.87),
             "runtime mapping should project hDOP and vDOP conservatively");
  ctx.Expect(!state.fix_valid &&
                 state.fix_type == universal_gnss::GnssFixType::kUnknown &&
                 !state.latitude_deg.has_value() &&
                 !state.longitude_deg.has_value() &&
                 !state.horizontal_accuracy_m.has_value() &&
                 !state.satellites_used.has_value() &&
                 !state.mean_cn0_db_hz.has_value(),
             "NAV-DOP should not invent fix, position, accuracy, satellite, or CN0 fields");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValidNavDopParsing(ctx);
  TestMalformedOrWrongFrames(ctx);
  TestRuntimeMappingBehavior(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols UBX NAV-DOP tests passed\n";
  return EXIT_SUCCESS;
}
