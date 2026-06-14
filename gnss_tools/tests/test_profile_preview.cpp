#include <cstdlib>
#include <iostream>
#include <optional>
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
                 result.commands.size() == 15u,
             "Unicore rover_high_precision preview should build the expected command count");
  ctx.Expect(result.summary.commands_total == 15u &&
                 result.summary.runtime_commands == 15u &&
                 result.summary.persistent_commands == 0u,
             "Unicore rover_high_precision preview summary should count runtime commands");
  ctx.Expect(!result.commands.empty() &&
                 result.commands.front().description == "set receiver mode to rover",
             "Unicore rover_high_precision preview should decode the MODE ROVER description");
  ctx.Expect(text.find("command: MODE ROVER") != std::string::npos &&
                 text.find("CONFIG SIGNALGROUP 3 6") != std::string::npos &&
                 text.find("UNLOG") != std::string::npos &&
                 text.find("LOG PVTSLNA ONTIME 1") != std::string::npos &&
                 text.find("SATSINFOA 1") != std::string::npos,
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
                 unicore_result.summary.commands_total == 18u &&
                 unicore_result.summary.runtime_commands == 16u &&
                 unicore_result.summary.persistent_commands == 1u &&
                 unicore_result.summary.factory_reset_commands == 1u,
             "persistent Unicore previews should expose the reset-first recovery workflow plus SAVECONFIG");
}

void TestUnicorePersistentTargetBaudPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision";
  options.persistent = true;
  options.baud = 460800u;

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.baud == std::optional<std::uint32_t>{460800u} &&
                 result.summary.commands_total == 18u &&
                 result.summary.runtime_commands == 16u &&
                 result.summary.persistent_commands == 1u &&
                 result.summary.factory_reset_commands == 1u,
             "persistent Unicore previews should preserve a distinct target config baud override");
  ctx.Expect(text.find("Config baud override: 460800") != std::string::npos &&
                 text.find("Factory reset baud: 115200") != std::string::npos &&
                 text.find("Target configured baud: 460800") != std::string::npos &&
                 text.find("CONFIG COM1 460800 8 n 1") != std::string::npos,
             "persistent Unicore preview text should distinguish override, factory baud, and target COM1 baud");
}

void TestSignalProfilePreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision";
  options.signal_profile =
      universal_gnss_driver::ReceiverAutoConfigSignalProfile::kMinimal;
  options.rate_hz = 1.0;

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.signal_profile ==
                     std::optional<universal_gnss_driver::ReceiverAutoConfigSignalProfile>{
                         universal_gnss_driver::ReceiverAutoConfigSignalProfile::kMinimal} &&
                 result.summary.commands_total == 12u,
             "minimal signal-profile preview should expose the reduced Unicore command set");
  ctx.Expect(text.find("Signal profile override: minimal") != std::string::npos &&
                 text.find("BESTNAVA 1") != std::string::npos &&
                 text.find("GPGSV") == std::string::npos,
             "minimal signal-profile preview text should show the reduced output plan");
}

void TestFactoryResetPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "factory_reset";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.commands.size() == 17u &&
                 result.summary.runtime_commands == 16u &&
                 result.summary.factory_reset_commands == 1u,
             "Unicore factory_reset preview should expose reset plus runtime recovery commands");
  ctx.Expect(!result.commands.empty() &&
                 result.commands.front().description.find("115200") != std::string::npos &&
                 text.find("command: FRESET") != std::string::npos &&
                 text.find("CONFIG COM1 921600") != std::string::npos,
             "factory_reset preview should document baud recovery and expose the recovery commands");
}

void TestJsonFormatting(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "ublox";
  options.profile = "rover_high_precision";
  options.signal_profile =
      universal_gnss_driver::ReceiverAutoConfigSignalProfile::kAllSignals;
  options.rate_hz = 1.0;

  const auto result = BuildProfilePreview(options);
  const std::string json = FormatProfilePreviewJson(result, true);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk,
             "JSON formatting test setup should build successfully");
  ctx.Expect(json.find("\"vendor\": \"ublox\"") != std::string::npos &&
                 json.find("\"profile\": \"rover_high_precision\"") != std::string::npos &&
                 json.find("\"signal_profile\": \"all_signals\"") != std::string::npos &&
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
  TestUnicorePersistentTargetBaudPreview(ctx);
  TestSignalProfilePreview(ctx);
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
