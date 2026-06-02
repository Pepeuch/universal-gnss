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

void TestUbloxRoverPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "ublox";
  options.profile = "rover";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.commands.size() == 13u,
             "u-blox rover preview should build the expected command count");
  ctx.Expect(result.summary.commands_total == 13u &&
                 result.summary.runtime_commands == 13u &&
                 result.summary.persistent_commands == 0u,
             "u-blox rover preview summary should count runtime commands");
  ctx.Expect(!result.commands.empty() &&
                 result.commands.front().description.find("measurement rate") != std::string::npos,
             "u-blox rover preview should decode the measurement-rate command description");
  ctx.Expect(text.find("Profile: ublox rover") != std::string::npos &&
                 text.find("NAV-PVT") != std::string::npos,
             "u-blox rover preview text should show profile metadata and decoded message outputs");
}

void TestUnicoreRoverPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "rover";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.commands.size() == 10u,
             "Unicore rover preview should build the expected command count");
  ctx.Expect(result.summary.commands_total == 10u &&
                 result.summary.runtime_commands == 10u &&
                 result.summary.persistent_commands == 0u,
             "Unicore rover preview summary should count runtime commands");
  ctx.Expect(!result.commands.empty() &&
                 result.commands.front().description == "set receiver mode to rover",
             "Unicore rover preview should decode the MODE ROVER description");
  ctx.Expect(text.find("command: MODE ROVER") != std::string::npos &&
                 text.find("LOG GPGGA ONTIME 0.2") != std::string::npos,
             "Unicore rover preview text should expose human-readable text commands");
}

void TestPersistentSummaryGeneration(TestContext& ctx)
{
  ProfilePreviewOptions ublox_options;
  ublox_options.vendor = "ublox";
  ublox_options.profile = "diagnostics";
  ublox_options.persistent = true;

  const auto ublox_result = BuildProfilePreview(ublox_options);
  ctx.Expect(ublox_result.status == ProfilePreviewStatus::kOk &&
                 ublox_result.summary.commands_total == 23u &&
                 ublox_result.summary.runtime_commands == 0u &&
                 ublox_result.summary.persistent_commands == 23u,
             "persistent u-blox previews should mark every generated command persistent");

  ProfilePreviewOptions unicore_options;
  unicore_options.vendor = "unicore";
  unicore_options.profile = "diagnostics";
  unicore_options.persistent = true;

  const auto unicore_result = BuildProfilePreview(unicore_options);
  ctx.Expect(unicore_result.status == ProfilePreviewStatus::kOk &&
                 unicore_result.summary.commands_total == 12u &&
                 unicore_result.summary.runtime_commands == 11u &&
                 unicore_result.summary.persistent_commands == 1u,
             "persistent Unicore previews should add only SAVECONFIG as a persistent command");
}

void TestJsonFormatting(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "ublox";
  options.profile = "base";
  options.rate_hz = 1.0;

  const auto result = BuildProfilePreview(options);
  const std::string json = FormatProfilePreviewJson(result, true);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk,
             "JSON formatting test setup should build successfully");
  ctx.Expect(json.find("\"vendor\": \"ublox\"") != std::string::npos &&
                 json.find("\"profile\": \"base\"") != std::string::npos &&
                 json.find("\"summary\"") != std::string::npos,
             "JSON preview output should include metadata and summary objects");
  ctx.Expect(json.find("\"hex\": \"") != std::string::npos,
             "verbose JSON preview output should include binary hex when requested");
}

void TestInvalidUnicoreBaudOverride(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "rover";
  options.baud = 921600u;

  const auto result = BuildProfilePreview(options);
  ctx.Expect(result.status == ProfilePreviewStatus::kInvalidArgument &&
                 result.error_message.find("--baud") != std::string::npos,
             "unsupported Unicore baud overrides should fail with a clear error");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestUbloxRoverPreview(ctx);
  TestUnicoreRoverPreview(ctx);
  TestPersistentSummaryGeneration(ctx);
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
