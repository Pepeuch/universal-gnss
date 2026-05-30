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

void TestUbloxRoverPlan(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "ublox";
  options.profile = "rover";

  const auto result = BuildConfigPlan(options);
  const std::string text = FormatConfigPlanText(result);

  ctx.Expect(result.status == ConfigPlanStatus::kOk &&
                 result.receiver_family == "F9/F10",
             "u-blox rover plan should resolve the expected receiver family");
  ctx.Expect(result.summary.commands_total == 9u &&
                 result.summary.runtime_commands == 9u &&
                 !result.summary.requires_explicit_safety_confirmation,
             "u-blox rover plan should remain runtime-only without extra confirmation");
  ctx.Expect(text.find("Dry run: yes") != std::string::npos &&
                 text.find("Command sequence:") != std::string::npos,
             "u-blox rover plan text should show dry-run status and command ordering");
}

void TestUnicoreDiagnosticsPlan(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "unicore";
  options.profile = "diagnostics";

  const auto result = BuildConfigPlan(options);
  const std::string text = FormatConfigPlanText(result);

  ctx.Expect(result.status == ConfigPlanStatus::kOk &&
                 result.receiver_family == "UM98x",
             "Unicore diagnostics plan should resolve the expected receiver family");
  ctx.Expect(result.summary.commands_total == 11u &&
                 result.summary.runtime_commands == 11u &&
                 result.summary.persistent_commands == 0u,
             "Unicore diagnostics plan should report the expected default command counts");
  ctx.Expect(text.find("MODE ROVER") != std::string::npos &&
                 text.find("SATSINFOA 1") != std::string::npos,
             "Unicore diagnostics plan text should show the ASCII command sequence");
}

void TestPersistentSafetySummary(TestContext& ctx)
{
  ConfigPlanOptions ublox_options;
  ublox_options.vendor = "ublox";
  ublox_options.profile = "rover";
  ublox_options.persistent = true;

  const auto ublox_result = BuildConfigPlan(ublox_options);
  ctx.Expect(ublox_result.status == ConfigPlanStatus::kOk &&
                 ublox_result.summary.persistent_commands == 9u &&
                 ublox_result.summary.commands_requiring_confirmation == 9u &&
                 ublox_result.summary.requires_explicit_safety_confirmation,
             "persistent u-blox plan should require confirmation for every command");

  ConfigPlanOptions unicore_options;
  unicore_options.vendor = "unicore";
  unicore_options.profile = "diagnostics";
  unicore_options.persistent = true;

  const auto unicore_result = BuildConfigPlan(unicore_options);
  ctx.Expect(unicore_result.status == ConfigPlanStatus::kOk &&
                 unicore_result.summary.persistent_commands == 1u &&
                 unicore_result.summary.commands_requiring_confirmation == 1u &&
                 unicore_result.summary.requires_explicit_safety_confirmation,
             "persistent Unicore plan should flag SAVECONFIG for confirmation");
}

void TestJsonFormatting(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "ublox";
  options.profile = "base";
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
  options.profile = "survey";

  const auto result = BuildConfigPlan(options);
  ctx.Expect(result.status == ConfigPlanStatus::kUnsupportedProfile &&
                 result.error_message.find("unsupported") != std::string::npos,
             "unsupported config plan profiles should be rejected clearly");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestUbloxRoverPlan(ctx);
  TestUnicoreDiagnosticsPlan(ctx);
  TestPersistentSafetySummary(ctx);
  TestJsonFormatting(ctx);
  TestUnsupportedProfileRejection(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools config plan tests passed\n";
  return EXIT_SUCCESS;
}
