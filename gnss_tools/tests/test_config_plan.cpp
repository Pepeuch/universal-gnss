#include <cstdlib>
#include <iostream>
#include <string>

#include "universal_gnss_tools/config_plan.hpp"

namespace
{

using universal_gnss_tools::BuildConfigPlan;
using universal_gnss_tools::ConfigPlanOptions;
using universal_gnss_tools::ConfigPlanStatus;
using universal_gnss_tools::FormatConfigPlanJson;
using universal_gnss_tools::FormatConfigPlanText;

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

void TestUbloxRoverHighPrecisionPlan(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "ublox";
  options.profile = "rover_high_precision";

  const auto result = BuildConfigPlan(options);
  const std::string text = FormatConfigPlanText(result);

  ctx.Expect(result.status == ConfigPlanStatus::kOk &&
                 result.receiver_family == "F9/F10",
             "u-blox rover_high_precision plan should resolve the expected receiver family");
  ctx.Expect(result.summary.commands_total == 13u &&
                 result.summary.runtime_commands == 13u &&
                 !result.summary.requires_explicit_safety_confirmation,
             "u-blox rover_high_precision plan should remain runtime-only without extra confirmation");
  ctx.Expect(text.find("Dry run: yes") != std::string::npos &&
                 text.find("Command sequence:") != std::string::npos,
             "u-blox rover_high_precision plan text should show dry-run status and command ordering");
}

void TestUnicoreDebugPlan(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision_debug";

  const auto result = BuildConfigPlan(options);
  const std::string text = FormatConfigPlanText(result);

  ctx.Expect(result.status == ConfigPlanStatus::kOk &&
                 result.receiver_family == "UM98x",
             "Unicore rover_high_precision_debug plan should resolve the expected receiver family");
  ctx.Expect(result.summary.commands_total == 15u &&
                 result.summary.runtime_commands == 15u &&
                 result.summary.persistent_commands == 0u,
             "Unicore rover_high_precision_debug plan should report the expected default command counts");
  ctx.Expect(text.find("MODE ROVER") != std::string::npos &&
                 text.find("CONFIG SIGNALGROUP 3 6") != std::string::npos &&
                 text.find("UNLOG") != std::string::npos &&
                 text.find("LOG PVTSLNA ONTIME 0.2") != std::string::npos,
             "Unicore rover_high_precision_debug plan text should show the verbose PVTSLNA command sequence");
}

void TestUnicorePersistentTargetBaudPlan(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision";
  options.persistent = true;
  options.baud = 460800u;

  const auto result = BuildConfigPlan(options);
  const std::string text = FormatConfigPlanText(result);

  ctx.Expect(result.status == ConfigPlanStatus::kOk &&
                 result.baud == std::optional<std::uint32_t>{460800u} &&
                 result.summary.commands_total == 18u &&
                 result.summary.runtime_commands == 16u &&
                 result.summary.persistent_commands == 1u &&
                 result.summary.factory_reset_commands == 1u,
             "persistent Unicore plans should preserve a distinct target config baud override");
  ctx.Expect(text.find("Config baud override: 460800") != std::string::npos &&
                 text.find("Factory reset baud: 115200") != std::string::npos &&
                 text.find("Target configured baud: 460800") != std::string::npos &&
                 text.find("CONFIG COM1 460800 8 n 1") != std::string::npos,
             "persistent Unicore plan text should distinguish override, factory baud, and target COM1 baud");
}

void TestRuntimeOnlyNoOpPlan(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "nmea";
  options.profile = "runtime_only";

  const auto result = BuildConfigPlan(options);
  const std::string text = FormatConfigPlanText(result);

  ctx.Expect(result.status == ConfigPlanStatus::kOk &&
                 result.summary.commands_total == 0u &&
                 result.receiver_family == "NMEA",
             "runtime_only config plans should support generic NMEA as a zero-command read-only profile");
  ctx.Expect(text.find("Command count: 0") != std::string::npos,
             "runtime_only plan text should report that no receiver commands are generated");
}

void TestPersistentSafetySummary(TestContext& ctx)
{
  ConfigPlanOptions ublox_options;
  ublox_options.vendor = "ublox";
  ublox_options.profile = "rover_high_precision";
  ublox_options.persistent = true;

  const auto ublox_result = BuildConfigPlan(ublox_options);
  ctx.Expect(ublox_result.status == ConfigPlanStatus::kOk &&
                 ublox_result.summary.persistent_commands == 13u &&
                 ublox_result.summary.commands_requiring_confirmation == 13u &&
                 ublox_result.summary.requires_explicit_safety_confirmation,
             "persistent u-blox plans should require confirmation for every command");

  ConfigPlanOptions unicore_options;
  unicore_options.vendor = "unicore";
  unicore_options.profile = "rover_high_precision_debug";
  unicore_options.persistent = true;

  const auto unicore_result = BuildConfigPlan(unicore_options);
  ctx.Expect(unicore_result.status == ConfigPlanStatus::kOk &&
                 unicore_result.summary.factory_reset_commands == 1u &&
                 unicore_result.summary.persistent_commands == 1u &&
                 unicore_result.summary.commands_requiring_confirmation == 2u &&
                 unicore_result.summary.requires_explicit_safety_confirmation,
             "persistent Unicore plans should require confirmation for reset plus SAVECONFIG");
}

void TestFactoryResetPlan(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "unicore";
  options.profile = "factory_reset";

  const auto result = BuildConfigPlan(options);
  const std::string text = FormatConfigPlanText(result);

  ctx.Expect(result.status == ConfigPlanStatus::kOk &&
                 result.summary.factory_reset_commands == 1u &&
                 result.summary.runtime_commands == 16u &&
                 result.production_ready &&
                 result.ready_to_execute,
             "factory_reset plans should expose the production-ready recovery sequence so operators can review it before execution");
  ctx.Expect(text.find("115200") != std::string::npos &&
                 text.find("reconnect/probe") != std::string::npos &&
                 text.find("CONFIG COM1 921600") != std::string::npos,
             "factory_reset plan text should document the baud reset, reprobe requirement, and COM1 recovery");
}

void TestJsonFormatting(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "ublox";
  options.profile = "rover_high_precision";
  options.rate_hz = 1.0;

  const auto result = BuildConfigPlan(options);
  const std::string json = FormatConfigPlanJson(result);

  ctx.Expect(result.status == ConfigPlanStatus::kOk,
             "JSON formatting test setup should build successfully");
  ctx.Expect(json.find("\"profile\": {") != std::string::npos &&
                 json.find("\"receiver_family\": \"F9/F10\"") != std::string::npos &&
                 json.find("\"summary\": {") != std::string::npos &&
                 json.find("\"commands\": [") != std::string::npos,
             "config plan JSON should include profile, summary, and command list objects");
  ctx.Expect(json.find("\"dry_run\": true") != std::string::npos,
             "config plan JSON should always mark the output as dry-run");
}

void TestUnsupportedProfileRejection(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "ublox";
  options.profile = "base";

  const auto result = BuildConfigPlan(options);
  ctx.Expect(result.status == ConfigPlanStatus::kUnsupportedProfile &&
                 result.error_message.find("unsupported") != std::string::npos,
             "unsupported config plan profiles should be rejected clearly");
}

void TestPersistentRuntimeOnlyRejected(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "nmea";
  options.profile = "runtime_only";
  options.persistent = true;

  const auto result = BuildConfigPlan(options);
  ctx.Expect(result.status == ConfigPlanStatus::kUnsupportedApplyMode &&
                 result.error_message.find("persistent apply") != std::string::npos,
             "runtime_only plans should reject persistent mode with a clear explanation");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestUbloxRoverHighPrecisionPlan(ctx);
  TestUnicoreDebugPlan(ctx);
  TestUnicorePersistentTargetBaudPlan(ctx);
  TestRuntimeOnlyNoOpPlan(ctx);
  TestPersistentSafetySummary(ctx);
  TestFactoryResetPlan(ctx);
  TestJsonFormatting(ctx);
  TestUnsupportedProfileRejection(ctx);
  TestPersistentRuntimeOnlyRejected(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools config plan tests passed\n";
  return EXIT_SUCCESS;
}
