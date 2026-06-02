#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/ubx_command_response_mapper.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_tools/config_apply.hpp"
#include "universal_gnss_transport/memory_stream.hpp"

namespace
{

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

void TestDryRunDoesNotRequirePort(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.vendor = "ublox";
  options.profile = "rover";

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.dry_run &&
                 !result.execute_requested &&
                 !result.executed &&
                 result.plan.summary.commands_total == 13u,
             "dry-run config apply should succeed without port or execute flags");
}

void TestExecuteWithoutConfirmRejected(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.vendor = "ublox";
  options.profile = "rover";
  options.execute = true;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kSafetyRejected &&
                 result.requires_runtime_confirmation &&
                 !result.execution_confirmed &&
                 result.error_message.find("--confirm-runtime") != std::string::npos,
             "execute mode should reject runtime plans unless --confirm-runtime is set");
}

void TestPersistentWithoutConfirmRejected(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.vendor = "ublox";
  options.profile = "rover";
  options.persistent = true;
  options.execute = true;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kSafetyRejected &&
                 !result.requires_runtime_confirmation &&
                 result.requires_persistent_confirmation &&
                 result.error_message.find("--confirm-persistent") != std::string::npos,
             "persistent config apply should reject execution without --confirm-persistent");
}

void TestPlanSafetySummaryCorrect(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.vendor = "unicore";
  options.profile = "diagnostics";
  options.persistent = true;

  const auto result = PrepareConfigApply(options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.requires_runtime_confirmation &&
                 result.requires_persistent_confirmation &&
                 result.plan.summary.runtime_commands == 11u &&
                 result.plan.summary.persistent_commands == 1u,
             "mixed runtime/persistent plans should surface both confirmation requirements");
}

void TestUnicoreRuntimeConfirmAccepted(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.vendor = "unicore";
  options.profile = "rover";
  options.execute = true;
  options.confirm_runtime = true;

  const auto prepared = PrepareConfigApply(options);
  const std::string responses =
      BuildRepeatedUnicoreOkResponses(prepared.plan.summary.commands_total);
  MemoryByteDuplex transport(
      std::vector<std::uint8_t>(responses.begin(), responses.end()));

  const auto result = ExecuteConfigApply(transport, options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 !result.dry_run &&
                 result.executed &&
                 result.execution_summary.commands_total == 10u &&
                 result.execution_summary.commands_completed == 10u &&
                 result.execution_summary.commands_failed == 0u &&
                 result.execution_summary.responses_applied == 10u &&
                 result.execution_summary.final_status == "completed",
             "confirmed runtime Unicore execution should complete against the in-memory duplex");
  ctx.Expect(!transport.written_bytes().empty(),
             "Unicore execution should dispatch command bytes to the transport");
}

void TestUbloxExecuteWithMemoryDuplex(TestContext& ctx)
{
  ConfigApplyOptions options;
  options.vendor = "ublox";
  options.profile = "rover";
  options.execute = true;
  options.confirm_runtime = true;

  const auto prepared = PrepareConfigApply(options);
  MemoryByteDuplex transport(BuildAckFramesForPlan(prepared));

  const auto result = ExecuteConfigApply(transport, options);

  ctx.Expect(result.status == ConfigApplyStatus::kOk &&
                 result.execution_summary.commands_total == 13u &&
                 result.execution_summary.commands_completed == 13u &&
                 result.execution_summary.commands_failed == 0u &&
                 result.execution_summary.responses_applied == 13u &&
                 result.execution_summary.final_status == "completed",
             "u-blox execution should consume ACK frames and complete through the UBX router path");
  ctx.Expect(!transport.written_bytes().empty(),
             "u-blox execution should write the generated UBX commands to the transport");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestDryRunDoesNotRequirePort(ctx);
  TestExecuteWithoutConfirmRejected(ctx);
  TestPersistentWithoutConfirmRejected(ctx);
  TestPlanSafetySummaryCorrect(ctx);
  TestUnicoreRuntimeConfirmAccepted(ctx);
  TestUbloxExecuteWithMemoryDuplex(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools config apply tests passed\n";
  return EXIT_SUCCESS;
}
