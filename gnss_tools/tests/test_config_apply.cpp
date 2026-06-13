#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_auto_config.hpp"
#include "universal_gnss_driver/receiver_discovery.hpp"
#include "universal_gnss_driver/ubx_command_response_mapper.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_tools/config_apply.hpp"
#include "universal_gnss_transport/memory_stream.hpp"

namespace
{

using universal_gnss_driver::ReceiverAutoConfigApplyMode;
using universal_gnss_driver::ReceiverAutoConfigProfile;
using universal_gnss_driver::ReceiverDetectedFamily;
using universal_gnss_driver::ReceiverPortSource;
using universal_gnss_driver::ReceiverProbeConfidence;
using universal_gnss_driver::ReceiverProbeResult;
using universal_gnss_driver::ReceiverTransportType;
using universal_gnss_driver::TryGetUbxCommandMessageIdentity;
using universal_gnss_tools::ConfigApplyOptions;
using universal_gnss_tools::ConfigApplyStatus;
using universal_gnss_tools::ExecuteConfigApply;
using universal_gnss_tools::PrepareConfigApply;
using universal_gnss_transport::MemoryByteDuplex;

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

ReceiverProbeResult MakeDiscoveryResult(const std::string& path,
                                        const std::uint32_t baud,
                                        const ReceiverDetectedFamily family)
{
  ReceiverProbeResult result;
  result.path = path;
  result.transport_type = ReceiverTransportType::kSerial;
  result.source = ReceiverPortSource::kExplicitPath;
  result.selected_baud = baud;
  result.detected_family = family;
  result.confidence = family == ReceiverDetectedFamily::kNmea
                          ? ReceiverProbeConfidence::kMedium
                          : ReceiverProbeConfidence::kHigh;
  result.discovery_score = family == ReceiverDetectedFamily::kNmea ? 20 : 100;
  result.reason = family == ReceiverDetectedFamily::kUblox
                      ? "valid_ubx_frame:+100"
                      : family == ReceiverDetectedFamily::kUnicore
                            ? "PVTSLNA:+100"
                            : family == ReceiverDetectedFamily::kNmea
                                  ? "valid_GGA:+20"
                                  : "no_data";
  result.note = result.reason;
  return result;
}

std::vector<std::uint8_t> BuildUbxFrame(std::uint8_t class_id,
                                        std::uint8_t message_id,
                                        const std::vector<std::uint8_t>& payload)
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
  return bytes;
}

std::vector<std::uint8_t> BuildAckFramesForPlan(
    const universal_gnss_tools::ConfigApplyResult& prepared)
{
  std::vector<std::uint8_t> bytes;
  for (const auto& plan_command : prepared.plan.commands)
  {
    const auto identity = TryGetUbxCommandMessageIdentity(plan_command.command);
    if (!identity.has_value())
    {
      std::cerr << "FAILED: test setup could not derive a UBX command identity\n";
      std::exit(EXIT_FAILURE);
    }

    const auto ack =
        BuildUbxFrame(0x05u, 0x01u, {identity->class_id, identity->message_id});
    bytes.insert(bytes.end(), ack.begin(), ack.end());
  }

  return bytes;
}

std::string BuildRepeatedUnicoreOkResponses(const std::size_t count)
{
  std::string text;
  for (std::size_t index = 0; index < count; ++index)
  {
    text += "<OK\r\n";
  }
  return text;
}

void TestDryRunDoesNotWrite(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/serial/by-id/f9p", 921600u, ReceiverDetectedFamily::kUblox);
  options.profile = ReceiverAutoConfigProfile::kRover;

  MemoryByteDuplex transport({});
  const auto result = ExecuteConfigApply(transport, options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.dry_run &&
                 !result.execute_requested &&
                 !result.executed &&
                 result.plan.summary.commands_total == 13u,
             "dry-run auto-config apply should succeed without dispatching commands");
  ctx.Expect(transport.written_bytes().empty(),
             "dry-run auto-config apply must not write to the transport");
}

void TestRuntimeOnlyRequiresConfirmation(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyACM0", 921600u, ReceiverDetectedFamily::kUblox);
  options.profile = ReceiverAutoConfigProfile::kRover;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kSafetyRejected &&
                 result.requires_runtime_confirmation &&
                 !result.execution_confirmed &&
                 result.error_message.find("--confirm") != std::string::npos,
             "runtime-only live apply should require explicit operator confirmation");
}

void TestUnknownReceiverRejected(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB99", 9600u, ReceiverDetectedFamily::kUnknown);
  options.profile = ReceiverAutoConfigProfile::kRover;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kUnsupportedReceiver &&
                 result.plan.unsupported_reason == "no_data",
             "unknown discovery results should be rejected before any live apply");
}

void TestNmeaRejected(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB9", 115200u, ReceiverDetectedFamily::kNmea);
  options.profile = ReceiverAutoConfigProfile::kRover;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kUnsupportedReceiver &&
                 result.plan.error_message.find("NMEA") != std::string::npos,
             "generic NMEA receivers should be rejected for auto-config apply");
}

void TestPersistentGuarded(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRover;
  options.apply_mode = ReceiverAutoConfigApplyMode::kPersistent;
  options.confirm = true;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kSafetyRejected &&
                 result.requires_runtime_confirmation &&
                 result.requires_persistent_confirmation &&
                 result.execution_confirmed &&
                 result.plan.summary.persistent_commands == 1u &&
                 result.error_message.find("persistent live apply remains guarded") !=
                     std::string::npos,
             "persistent live apply should remain guarded even after confirmation");
}

void TestUnicoreRuntimeApplyStillWorks(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  options.profile = ReceiverAutoConfigProfile::kRover;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  const std::string responses =
      BuildRepeatedUnicoreOkResponses(prepared.plan.summary.commands_total);
  MemoryByteDuplex transport(
      std::vector<std::uint8_t>(responses.begin(), responses.end()));

  const auto result = ExecuteConfigApply(transport, options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 !result.dry_run &&
                 result.executed &&
                 result.execution_summary.commands_total == 13u &&
                 result.execution_summary.commands_completed == 13u &&
                 result.execution_summary.commands_failed == 0u &&
                 result.execution_summary.responses_applied == 13u &&
                 result.execution_summary.final_status == "completed",
             "confirmed runtime-only Unicore apply should complete against the in-memory duplex");
  ctx.Expect(!transport.written_bytes().empty(),
             "runtime-only Unicore apply should write command bytes to the transport");
}

void TestUbloxRuntimeApplyStillWorks(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.discovery_result =
      MakeDiscoveryResult("/dev/serial/by-id/f9p", 921600u, ReceiverDetectedFamily::kUblox);
  options.profile = ReceiverAutoConfigProfile::kRover;
  options.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  options.confirm = true;

  const auto prepared = PrepareConfigApply(options);
  MemoryByteDuplex transport(BuildAckFramesForPlan(prepared));

  const auto result = ExecuteConfigApply(transport, options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.execution_summary.commands_total == 13u &&
                 result.execution_summary.commands_completed == 13u &&
                 result.execution_summary.commands_failed == 0u &&
                 result.execution_summary.responses_applied == 13u &&
                 result.execution_summary.final_status == "completed",
             "confirmed runtime-only u-blox apply should complete through the UBX router path");
  ctx.Expect(!transport.written_bytes().empty(),
             "runtime-only u-blox apply should write the planned UBX commands");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestDryRunDoesNotWrite(ctx);
  TestRuntimeOnlyRequiresConfirmation(ctx);
  TestUnknownReceiverRejected(ctx);
  TestNmeaRejected(ctx);
  TestPersistentGuarded(ctx);
  TestUnicoreRuntimeApplyStillWorks(ctx);
  TestUbloxRuntimeApplyStillWorks(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools config apply tests passed\n";
  return EXIT_SUCCESS;
}
