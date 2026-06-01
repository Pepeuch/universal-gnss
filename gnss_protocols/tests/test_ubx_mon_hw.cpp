#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/ubx_parser.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssDiagnosticSeverity;
using universal_gnss::GnssRuntimeState;
using universal_gnss::HasCapability;
using universal_gnss::HasValueAvailable;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UbxAntennaPower;
using universal_gnss_protocols::UbxAntennaStatus;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_protocols::UbxMonHwLayout;
using universal_gnss_protocols::UbxMonRfJammingState;

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

void WriteLeU2(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::uint16_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void WriteLeU4(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::uint32_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
  payload[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
  payload[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

UbxFrame BuildUbxFrame(const std::uint8_t class_id,
                       const std::uint8_t message_id,
                       const std::vector<std::uint8_t>& payload,
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

std::vector<std::uint8_t> MakeMonHwPayload(const std::uint8_t antenna_status,
                                           const std::uint8_t antenna_power,
                                           const std::uint8_t jamming_state)
{
  std::vector<std::uint8_t> payload(60u, 0u);
  WriteLeU2(payload, 16u, 180u);
  WriteLeU2(payload, 18u, 4096u);
  payload[20u] = antenna_status;
  payload[21u] = antenna_power;
  payload[22u] = static_cast<std::uint8_t>(0x01u | ((jamming_state & 0x03u) << 2u));
  payload[45u] = 17u;
  return payload;
}

std::vector<std::uint8_t> MakeReservedMonHwPayload()
{
  return std::vector<std::uint8_t>(56u, 0u);
}

std::vector<std::uint8_t> MakeMonHw2Payload()
{
  std::vector<std::uint8_t> payload(28u, 0u);
  payload[0u] = static_cast<std::uint8_t>(-3);
  payload[1u] = 120u;
  payload[2u] = static_cast<std::uint8_t>(5);
  payload[3u] = 110u;
  payload[4u] = 112u;
  WriteLeU4(payload, 8u, 0x12345678u);
  WriteLeU4(payload, 20u, 0xAABBCCDDu);
  return payload;
}

void TestValidMonHwParseAndDiagnostics(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::ParseUbxMonHw(
      BuildUbxFrame(0x0Au, 0x09u, MakeMonHwPayload(2u, 1u, 2u), 1234));

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid MON-HW frame should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(1234) &&
                 record.layout == UbxMonHwLayout::kClassic &&
                 record.noise_per_ms == std::optional<std::uint16_t>(180u) &&
                 record.agc_count == std::optional<std::uint16_t>(4096u) &&
                 record.antenna_status == std::optional<UbxAntennaStatus>(UbxAntennaStatus::kOk) &&
                 record.antenna_power == std::optional<UbxAntennaPower>(UbxAntennaPower::kOn) &&
                 record.jamming_state == UbxMonRfJammingState::kWarning &&
                 record.cw_suppression == std::optional<std::uint8_t>(17u),
             "MON-HW should decode documented classic hardware fields");

  const auto diagnostics = universal_gnss_protocols::UbxMonHwToDiagnosticEvents(record);
  ctx.Expect(diagnostics.size() == 2u,
             "MON-HW should emit antenna and jamming diagnostics when documented states are known");
  if (diagnostics.size() == 2u)
  {
    ctx.Expect(diagnostics[0].severity == GnssDiagnosticSeverity::kOk &&
                   diagnostics[0].code == "ubx_mon_hw.antenna_ok",
               "MON-HW antenna-ok state should become a receiver-ok diagnostic");
    ctx.Expect(diagnostics[1].severity == GnssDiagnosticSeverity::kWarning &&
                   diagnostics[1].code == "ubx_mon_hw.jamming_warning",
               "MON-HW jamming warning should become a receiver warning diagnostic");
  }

  const GnssRuntimeState state = universal_gnss_protocols::UbxMonHwToRuntimeState(record);
  ctx.Expect(HasCapability(state, GnssCapability::kInterferenceState) &&
                 HasCapability(state, GnssCapability::kJammingState) &&
                 HasValueAvailable(state, GnssCapability::kInterferenceState) &&
                 HasValueAvailable(state, GnssCapability::kJammingState) &&
                 state.interference_detected == std::optional<bool>(true) &&
                 state.jamming_detected == std::optional<bool>(true),
             "MON-HW warning jamming state should map to portable interference/jamming booleans");
  ctx.Expect(state.fix_type == universal_gnss::GnssFixType::kUnknown &&
                 !state.latitude_deg.has_value() &&
                 !state.horizontal_accuracy_m.has_value(),
             "MON-HW runtime mapping should not invent fix or position fields");
}

void TestReservedMonHwLayoutAndMonHw2Parse(TestContext& ctx)
{
  const auto reserved = universal_gnss_protocols::ParseUbxMonHw(
      BuildUbxFrame(0x0Au, 0x09u, MakeReservedMonHwPayload(), 2000));
  ctx.Expect(reserved.status == ParserStatus::kRecordReady && reserved.record.has_value(),
             "documented reserved MON-HW payload should still parse structurally");
  if (reserved.record.has_value())
  {
    ctx.Expect(reserved.record->layout == UbxMonHwLayout::kReserved &&
                   universal_gnss_protocols::UbxMonHwToDiagnosticEvents(*reserved.record).empty(),
               "reserved MON-HW layout should stay semantic-light and diagnostics-free");
  }

  const auto mon_hw2 = universal_gnss_protocols::ParseUbxMonHw2(
      BuildUbxFrame(0x0Au, 0x0Bu, MakeMonHw2Payload(), 2001));
  ctx.Expect(mon_hw2.status == ParserStatus::kRecordReady && mon_hw2.record.has_value(),
             "valid MON-HW2 frame should parse successfully");
  if (!mon_hw2.record.has_value())
  {
    return;
  }

  ctx.Expect(mon_hw2.record->timestamp_ns == std::optional<std::int64_t>(2001) &&
                 mon_hw2.record->ofs_i == -3 &&
                 mon_hw2.record->mag_i == 120u &&
                 mon_hw2.record->ofs_q == 5 &&
                 mon_hw2.record->mag_q == 110u &&
                 mon_hw2.record->cfg_source == 112u &&
                 mon_hw2.record->low_level_configuration == 0x12345678u &&
                 mon_hw2.record->post_status == 0xAABBCCDDu,
             "MON-HW2 should decode documented extended hardware status fields");
}

void TestAntennaFaultDiagnosticsAndMalformedFrames(TestContext& ctx)
{
  const auto shorted = universal_gnss_protocols::ParseUbxMonHw(
      BuildUbxFrame(0x0Au, 0x09u, MakeMonHwPayload(3u, 0u, 3u), 3000));
  ctx.Expect(shorted.record.has_value(),
             "antenna-short diagnostic test requires a parsed MON-HW record");
  if (shorted.record.has_value())
  {
    const auto diagnostics = universal_gnss_protocols::UbxMonHwToDiagnosticEvents(*shorted.record);
    ctx.Expect(diagnostics.size() >= 2u &&
                   diagnostics[0].severity == GnssDiagnosticSeverity::kError &&
                   diagnostics[0].code == "ubx_mon_hw.antenna_short",
               "antenna short should map to a receiver error");
  }

  const UbxFrame wrong_message = BuildUbxFrame(0x01u, 0x09u, MakeMonHwPayload(2u, 1u, 1u));
  ctx.Expect(universal_gnss_protocols::ParseUbxMonHw(wrong_message).status ==
                 ParserStatus::kSkipped,
             "wrong UBX class/id should be skipped for MON-HW");
  ctx.Expect(universal_gnss_protocols::ParseUbxMonHw2(wrong_message).status ==
                 ParserStatus::kSkipped,
             "wrong UBX class/id should be skipped for MON-HW2");

  auto short_payload = MakeMonHwPayload(2u, 1u, 1u);
  short_payload.pop_back();
  ctx.Expect(universal_gnss_protocols::ParseUbxMonHw(
                 BuildUbxFrame(0x0Au, 0x09u, short_payload)).status ==
                 ParserStatus::kInvalidData,
             "truncated MON-HW classic payload should be rejected");

  auto short_hw2 = MakeMonHw2Payload();
  short_hw2.pop_back();
  ctx.Expect(universal_gnss_protocols::ParseUbxMonHw2(
                 BuildUbxFrame(0x0Au, 0x0Bu, short_hw2)).status ==
                 ParserStatus::kInvalidData,
             "truncated MON-HW2 payload should be rejected");

  UbxFrame invalid_checksum = BuildUbxFrame(0x0Au, 0x09u, MakeMonHwPayload(2u, 1u, 1u));
  invalid_checksum.checksum_status = ChecksumStatus::kInvalid;
  ctx.Expect(universal_gnss_protocols::ParseUbxMonHw(invalid_checksum).status ==
                 ParserStatus::kInvalidData,
             "invalid checksum status should reject MON-HW records");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValidMonHwParseAndDiagnostics(ctx);
  TestReservedMonHwLayoutAndMonHw2Parse(ctx);
  TestAntennaFaultDiagnosticsAndMalformedFrames(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols UBX MON-HW tests passed\n";
  return EXIT_SUCCESS;
}
