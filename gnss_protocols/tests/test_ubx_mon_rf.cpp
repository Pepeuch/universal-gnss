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
using universal_gnss_protocols::UbxMonRfJammingState;

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

std::vector<std::uint8_t> MakeMonRfPayload(std::uint8_t first_jamming_state,
                                           std::uint8_t second_jamming_state = 1u)
{
  std::vector<std::uint8_t> payload(4u + (2u * 24u), 0u);
  payload[0u] = 0x00u;
  payload[1u] = 2u;

  payload[4u] = 0u;
  payload[5u] = first_jamming_state;
  payload[6u] = 0x02u;
  payload[7u] = 0x01u;
  WriteLeU4(payload, 8u, 0x12345678u);
  WriteLeU2(payload, 16u, 150u);
  WriteLeU2(payload, 18u, 4096u);
  payload[20u] = 20u;

  payload[28u] = 1u;
  payload[29u] = second_jamming_state;
  payload[30u] = 0x02u;
  payload[31u] = 0x01u;
  WriteLeU4(payload, 32u, 0x87654321u);
  WriteLeU2(payload, 40u, 250u);
  WriteLeU2(payload, 42u, 5000u);
  payload[44u] = 0u;

  return payload;
}

void TestValidMonRfNoJamming(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxMonRf(
      BuildUbxFrame(0x0Au, 0x38u, MakeMonRfPayload(1u), 900));

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid MON-RF frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(900),
             "MON-RF should preserve the framing timestamp");
  ctx.Expect(record.version == 0x00u && record.block_count == 2u,
             "MON-RF should decode version and block count");
  ctx.Expect(record.blocks[0].block_id == 0u &&
                 record.blocks[0].jamming_state == UbxMonRfJammingState::kOk &&
                 record.blocks[0].noise_per_ms == 150u &&
                 record.blocks[0].agc_count == 4096u &&
                 record.blocks[0].cw_suppression == 20u,
             "MON-RF should decode documented RF block fields");
}

void TestValidMonRfJammingState(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxMonRf(
      BuildUbxFrame(0x0Au, 0x38u, MakeMonRfPayload(3u), 901));
  ctx.Expect(result.record.has_value(), "jamming test requires a parsed MON-RF record");
  if (!result.record.has_value())
  {
    return;
  }

  const GnssRuntimeState state = universal_gnss_protocols::UbxMonRfToRuntimeState(*result.record);
  ctx.Expect(HasCapability(state, GnssCapability::kInterferenceState) &&
                 HasCapability(state, GnssCapability::kJammingState),
             "runtime mapping should advertise RF monitor capabilities");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kInterferenceState) &&
                 HasValueAvailable(state, GnssCapability::kJammingState),
             "known MON-RF jamming states should produce value flags");
  ctx.Expect(state.interference_detected == std::optional<bool>(true) &&
                 state.jamming_detected == std::optional<bool>(true),
             "warning/critical MON-RF states should map to detected RF issues");
}

void TestMalformedOrWrongFrames(TestContext& ctx)
{
  const UbxFrame wrong_message = BuildUbxFrame(0x01u, 0x35u, std::vector<std::uint8_t>(8u, 0u));
  ctx.Expect(universal_gnss_protocols::ParseUbxMonRf(wrong_message).status == ParserStatus::kSkipped,
             "wrong UBX class/id should be skipped");

  auto short_payload = MakeMonRfPayload(1u);
  short_payload.pop_back();
  ctx.Expect(universal_gnss_protocols::ParseUbxMonRf(BuildUbxFrame(0x0Au, 0x38u, short_payload)).status ==
                 ParserStatus::kInvalidData,
             "truncated MON-RF payload should be rejected");

  auto wrong_version = MakeMonRfPayload(1u);
  wrong_version[0u] = 0x01u;
  ctx.Expect(universal_gnss_protocols::ParseUbxMonRf(BuildUbxFrame(0x0Au, 0x38u, wrong_version)).status ==
                 ParserStatus::kInvalidData,
             "unsupported MON-RF version should be rejected");

  UbxFrame invalid_checksum = BuildUbxFrame(0x0Au, 0x38u, MakeMonRfPayload(1u));
  invalid_checksum.checksum_status = ChecksumStatus::kInvalid;
  ctx.Expect(universal_gnss_protocols::ParseUbxMonRf(invalid_checksum).status ==
                 ParserStatus::kInvalidData,
             "invalid checksum status should be rejected");
}

void TestMultipleBlocksAndNoInventedFixFields(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxMonRf(
      BuildUbxFrame(0x0Au, 0x38u, MakeMonRfPayload(1u, 2u), 902));
  ctx.Expect(result.record.has_value(), "multi-block test requires a parsed MON-RF record");
  if (!result.record.has_value())
  {
    return;
  }

  const GnssRuntimeState state = universal_gnss_protocols::UbxMonRfToRuntimeState(*result.record);
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(902),
             "runtime mapping should preserve the framing timestamp");
  ctx.Expect(state.interference_detected == std::optional<bool>(true) &&
                 state.jamming_detected == std::optional<bool>(true),
             "any warning block should make the aggregate RF state true");
  ctx.Expect(!HasCapability(state, GnssCapability::kRtkMode) &&
                 !state.rtk_mode.has_value() && !state.latitude_deg.has_value() &&
                 !state.horizontal_accuracy_m.has_value(),
             "MON-RF mapping should not invent fix, RTK, or accuracy fields");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValidMonRfNoJamming(ctx);
  TestValidMonRfJammingState(ctx);
  TestMalformedOrWrongFrames(ctx);
  TestMultipleBlocksAndNoInventedFixFields(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols UBX MON-RF tests passed\n";
  return EXIT_SUCCESS;
}
