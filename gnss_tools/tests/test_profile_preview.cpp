#include <cstdlib>
#include <iostream>
#include <string>

#include "universal_gnss_tools/profile_preview.hpp"

namespace
{

using universal_gnss_tools::BuildProfilePreview;
using universal_gnss_tools::FormatProfilePreviewJson;
using universal_gnss_tools::FormatProfilePreviewText;
using universal_gnss_tools::ProfilePreviewOptions;
using universal_gnss_tools::ProfilePreviewStatus;

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

void TestUbloxRoverHighPrecisionPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "ublox";
  options.profile = "rover_high_precision";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.commands.size() == 13u,
             "u-blox rover_high_precision preview should build the expected command count");
  ctx.Expect(result.summary.commands_total == 13u &&
                 result.summary.runtime_commands == 13u &&
                 result.summary.persistent_commands == 0u,
             "u-blox rover_high_precision preview summary should count runtime commands");
  ctx.Expect(!result.commands.empty() &&
                 result.commands.front().description.find("measurement rate") != std::string::npos,
             "u-blox rover_high_precision preview should decode the measurement-rate command description");
  ctx.Expect(text.find("Profile: ublox rover_high_precision") != std::string::npos &&
                 text.find("NAV-PVT") != std::string::npos,
             "u-blox rover_high_precision preview text should show profile metadata and decoded message outputs");
}

void TestUnicoreRoverHighPrecisionPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.commands.size() == 13u,
             "Unicore rover_high_precision preview should build the expected command count");
  ctx.Expect(result.summary.commands_total == 13u &&
                 result.summary.runtime_commands == 13u &&
                 result.summary.persistent_commands == 0u,
             "Unicore rover_high_precision preview summary should count runtime commands");
  ctx.Expect(!result.commands.empty() &&
                 result.commands.front().description == "set receiver mode to rover",
             "Unicore rover_high_precision preview should decode the MODE ROVER description");
  ctx.Expect(text.find("command: MODE ROVER") != std::string::npos &&
                 text.find("UNLOG") != std::string::npos &&
                 text.find("GPGSV 1") != std::string::npos,
             "Unicore rover_high_precision preview text should expose human-readable text commands");
}

void TestRuntimeOnlyPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "nmea";
  options.profile = "runtime_only";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.commands.empty() &&
                 result.summary.commands_total == 0u,
             "runtime_only preview should support a zero-command read-only plan");
  ctx.Expect(text.find("Profile: nmea runtime_only") != std::string::npos,
             "runtime_only preview text should still show the canonical profile metadata");
}

void TestPersistentSummaryGeneration(TestContext& ctx)
{
  ProfilePreviewOptions ublox_options;
  ublox_options.vendor = "ublox";
  ublox_options.profile = "rover_high_precision_debug";
  ublox_options.persistent = true;

  const auto ublox_result = BuildProfilePreview(ublox_options);
  ctx.Expect(ublox_result.status == ProfilePreviewStatus::kOk &&
                 ublox_result.summary.commands_total == 23u &&
                 ublox_result.summary.runtime_commands == 0u &&
                 ublox_result.summary.persistent_commands == 23u,
             "persistent u-blox previews should mark every generated command persistent");

  ProfilePreviewOptions unicore_options;
  unicore_options.vendor = "unicore";
  unicore_options.profile = "rover_high_precision_debug";
  unicore_options.persistent = true;

  const auto unicore_result = BuildProfilePreview(unicore_options);
  ctx.Expect(unicore_result.status == ProfilePreviewStatus::kOk &&
                 unicore_result.summary.commands_total == 15u &&
                 unicore_result.summary.runtime_commands == 14u &&
                 unicore_result.summary.persistent_commands == 1u,
             "persistent Unicore previews should add only SAVECONFIG as a persistent command");
}

void TestFactoryResetPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "factory_reset";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.commands.size() == 1u &&
                 result.summary.factory_reset_commands == 1u,
             "Unicore factory_reset preview should generate one factory-reset command");
  ctx.Expect(!result.commands.empty() &&
                 result.commands.front().description.find("115200") != std::string::npos,
             "factory_reset preview should describe the baud-reset implication");
  ctx.Expect(text.find("command: FRESET") != std::string::npos,
             "factory_reset preview text should expose the exact command");
}

void TestJsonFormatting(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "ublox";
  options.profile = "rover_high_precision";
  options.rate_hz = 1.0;

  const auto result = BuildProfilePreview(options);
  const std::string json = FormatProfilePreviewJson(result, true);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk,
             "JSON formatting test setup should build successfully");
  ctx.Expect(json.find("\"vendor\": \"ublox\"") != std::string::npos &&
                 json.find("\"profile\": \"rover_high_precision\"") != std::string::npos &&
                 json.find("\"summary\"") != std::string::npos,
             "JSON preview output should include metadata and summary objects");
  ctx.Expect(json.find("\"hex\": \"") != std::string::npos,
             "verbose JSON preview output should include binary hex when requested");
}

void TestInvalidUnicoreBaudOverride(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision";
  options.baud = 921600u;

  const auto result = BuildProfilePreview(options);
  ctx.Expect(result.status == ProfilePreviewStatus::kInvalidArgument &&
                 result.error_message.find("baud") != std::string::npos,
             "unsupported Unicore baud overrides should fail with a clear error");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestUbloxRoverHighPrecisionPreview(ctx);
  TestUnicoreRoverHighPrecisionPreview(ctx);
  TestRuntimeOnlyPreview(ctx);
  TestPersistentSummaryGeneration(ctx);
  TestFactoryResetPreview(ctx);
  TestJsonFormatting(ctx);
  TestInvalidUnicoreBaudOverride(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools profile preview tests passed\n";
  return EXIT_SUCCESS;
}
