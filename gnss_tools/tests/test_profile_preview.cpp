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

bool HasTextCommand(const universal_gnss_tools::ProfilePreviewResult& result,
                    const std::string& command_text)
{
  for (const auto& command : result.commands)
  {
    if (command.command.payload.kind == universal_gnss_driver::ReceiverCommandPayloadKind::kText &&
        command.command.payload.text.find(command_text) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

bool ContainsWarning(const universal_gnss_tools::ProfilePreviewResult& result,
                     const std::string& needle)
{
  for (const auto& warning : result.warnings)
  {
    if (warning.find(needle) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

void TestUbloxRoverHighPrecisionPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "ublox";
  options.profile = "rover_high_precision";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk && result.commands.size() == 13u,
             "u-blox rover_high_precision preview should build the expected command count");
  ctx.Expect(result.summary.commands_total == 13u && result.summary.runtime_commands == 13u &&
                 result.summary.persistent_commands == 0u,
             "u-blox rover_high_precision preview summary should count runtime commands");
  ctx.Expect(
      !result.commands.empty() &&
          result.commands.front().description.find("measurement rate") != std::string::npos,
      "u-blox rover_high_precision preview should decode the measurement-rate command description");
  ctx.Expect(text.find("Profile: ublox rover_high_precision") != std::string::npos &&
                 text.find("NAV-PVT") != std::string::npos,
             "u-blox rover_high_precision preview text should show profile metadata and decoded "
             "message outputs");
}

void TestUnicoreRoverHighPrecisionPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kUnsupportedVendor &&
                 result.commands.empty() && result.summary.commands_total == 0u,
             "Unicore rover_high_precision preview must block mutation when the model is unknown");
  ctx.Expect(text.find("requires an explicitly recognized model") != std::string::npos &&
                 !HasTextCommand(result, "MODE ROVER") &&
                 !HasTextCommand(result, "CONFIG SIGNALGROUP"),
             "unknown-model preview text must expose the safety stop and no commands");

  options.receiver_model = "UM982";
  const auto um982_result = BuildProfilePreview(options);
  const std::string um982_text = FormatProfilePreviewText(um982_result);
  ctx.Expect(um982_result.status == ProfilePreviewStatus::kOk &&
                 um982_result.receiver_model == std::optional<std::string>{"UM982"} &&
                 um982_result.commands.size() == 13u &&
                 um982_result.commands.front().description ==
                     "set receiver mode to rover survey lawn mower" &&
                 um982_text.find("MODE ROVER SURVEY MOW") != std::string::npos &&
                 !HasTextCommand(um982_result, "CONFIG SIGNALGROUP") &&
                 ContainsWarning(um982_result, "Build7650+"),
             "UM982 preview should expose the documented rover mode without forcing "
             "CONFIG SIGNALGROUP");
}

void TestRuntimeOnlyPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "nmea";
  options.profile = "runtime_only";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk && result.commands.empty() &&
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
  unicore_options.receiver_model = "UM981";

  const auto unicore_result = BuildProfilePreview(unicore_options);
  ctx.Expect(unicore_result.status == ProfilePreviewStatus::kOk &&
                 unicore_result.summary.commands_total == 16u &&
                 unicore_result.summary.runtime_commands == 14u &&
                 unicore_result.summary.persistent_commands == 1u &&
                 unicore_result.summary.factory_reset_commands == 1u,
             "documented persistent Unicore previews should expose the reset-first recovery "
             "workflow plus SAVECONFIG without guessing signal groups");
}

void TestUnicorePersistentTargetBaudPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision";
  options.receiver_model = "UM982";
  options.persistent = true;
  options.baud = 460800u;

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.baud == std::optional<std::uint32_t>{460800u} &&
                 result.summary.commands_total == 17u && result.summary.runtime_commands == 15u &&
                 result.summary.persistent_commands == 1u &&
                 result.summary.factory_reset_commands == 1u,
             "persistent Unicore previews should preserve a distinct target config baud override");
  ctx.Expect(text.find("Config baud override: 460800") != std::string::npos &&
                 text.find("Factory reset baud: 115200") != std::string::npos &&
                 text.find("Target configured baud: 460800") != std::string::npos &&
                 text.find("CONFIG COM1 460800 8 n 1") != std::string::npos,
             "persistent Unicore preview text should distinguish override, factory baud, and "
             "target COM1 baud");
}

void TestSignalProfilePreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision";
  options.receiver_model = "UM982";
  options.signal_profile = universal_gnss_driver::ReceiverAutoConfigSignalProfile::kMinimal;
  options.rate_hz = 1.0;

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.signal_profile ==
                     std::optional<universal_gnss_driver::ReceiverAutoConfigSignalProfile>{
                         universal_gnss_driver::ReceiverAutoConfigSignalProfile::kMinimal} &&
                 result.summary.commands_total == 10u,
             "minimal signal-profile preview should expose the reduced Unicore command set");
  ctx.Expect(text.find("Signal profile override: minimal") != std::string::npos &&
                 text.find("BESTNAVA 1") != std::string::npos &&
                 text.find("GPGSV") == std::string::npos,
             "minimal signal-profile preview text should show the reduced output plan");

  ProfilePreviewOptions exact_rate_options = options;
  exact_rate_options.signal_profile.reset();
  exact_rate_options.rate_hz = 5.0;
  const auto exact_five_hz_result = BuildProfilePreview(exact_rate_options);
  const std::string exact_five_hz_text = FormatProfilePreviewText(exact_five_hz_result);
  ctx.Expect(exact_five_hz_result.status == ProfilePreviewStatus::kOk &&
                 exact_five_hz_text.find("BESTNAVA 0.2") != std::string::npos &&
                 !ContainsWarning(exact_five_hz_result, "using "),
             "exact 5 Hz Unicore preview should preserve the documented 0.2 s BESTNAVA period "
             "without normalization warnings");

  exact_rate_options.rate_hz = 10.0;
  const auto exact_ten_hz_result = BuildProfilePreview(exact_rate_options);
  const std::string exact_ten_hz_text = FormatProfilePreviewText(exact_ten_hz_result);
  ctx.Expect(exact_ten_hz_result.status == ProfilePreviewStatus::kOk &&
                 exact_ten_hz_text.find("BESTNAVA 0.1") != std::string::npos &&
                 !ContainsWarning(exact_ten_hz_result, "using "),
             "exact 10 Hz Unicore preview should preserve the documented 0.1 s BESTNAVA period "
             "without normalization warnings");

  exact_rate_options.rate_hz = 7.0;
  const auto rounded_rate_result = BuildProfilePreview(exact_rate_options);
  const std::string rounded_rate_text = FormatProfilePreviewText(rounded_rate_result);
  ctx.Expect(rounded_rate_result.status == ProfilePreviewStatus::kOk &&
                 rounded_rate_text.find("BESTNAVA 0.2") != std::string::npos &&
                 ContainsWarning(rounded_rate_result, "using 5 Hz instead") &&
                 rounded_rate_text.find("0.143") == std::string::npos,
             "unsupported 7 Hz Unicore preview should normalize to the documented 5 Hz period and "
             "must not emit 0.143 s");

  ProfilePreviewOptions unknown_model_options;
  unknown_model_options.vendor = "unicore";
  unknown_model_options.profile = "rover_high_precision";
  unknown_model_options.receiver_model = "UM952";
  unknown_model_options.signal_profile =
      universal_gnss_driver::ReceiverAutoConfigSignalProfile::kHighPrecision;
  const auto unknown_model_result = BuildProfilePreview(unknown_model_options);
  const std::string unknown_model_text = FormatProfilePreviewText(unknown_model_result);
  ctx.Expect(unknown_model_result.status == ProfilePreviewStatus::kUnsupportedVendor &&
                 unknown_model_result.receiver_model == std::optional<std::string>{"UM952"} &&
                 unknown_model_result.summary.commands_total == 0u &&
                 unknown_model_result.error_message.find("UM952") != std::string::npos &&
                 unknown_model_text.find("requires an explicitly recognized model") !=
                     std::string::npos &&
                 !HasTextCommand(unknown_model_result, "CONFIG SIGNALGROUP"),
             "unknown-model preview should block all mutating configuration");

  ProfilePreviewOptions known_non_baseline_options;
  known_non_baseline_options.vendor = "unicore";
  known_non_baseline_options.profile = "rover_high_precision";
  known_non_baseline_options.receiver_model = "UM960";
  const auto known_non_baseline_result = BuildProfilePreview(known_non_baseline_options);
  const std::string known_non_baseline_text = FormatProfilePreviewText(known_non_baseline_result);
  ctx.Expect(known_non_baseline_result.status == ProfilePreviewStatus::kOk &&
                 known_non_baseline_result.receiver_model == std::optional<std::string>{"UM960"} &&
                 known_non_baseline_result.summary.commands_total == 13u &&
                 known_non_baseline_text.find("Receiver model: UM960") != std::string::npos &&
                 known_non_baseline_text.find("safe generic non-baseline fallback") ==
                     std::string::npos &&
                 !HasTextCommand(known_non_baseline_result, "CONFIG SIGNALGROUP"),
             "known non-baseline Unicore models such as UM960 should be accepted without falling "
             "back to the unknown-model path");
}

void TestUbloxOutputPortPreview(TestContext& ctx)
{
  ProfilePreviewOptions usb_options;
  usb_options.vendor = "ublox";
  usb_options.profile = "rover_high_precision";
  usb_options.output_port = universal_gnss_driver::ReceiverAutoConfigOutputPort::kUsb;
  usb_options.baud = 460800u;

  const auto usb_result = BuildProfilePreview(usb_options);
  const std::string usb_text = FormatProfilePreviewText(usb_result);

  ctx.Expect(usb_result.status == ProfilePreviewStatus::kOk &&
                 usb_result.summary.commands_total == 9u &&
                 usb_result.output_port ==
                     std::optional<universal_gnss_driver::ReceiverAutoConfigOutputPort>{
                         universal_gnss_driver::ReceiverAutoConfigOutputPort::kUsb} &&
                 usb_result.resolved_output_port ==
                     std::optional<universal_gnss_driver::ReceiverAutoConfigOutputPort>{
                         universal_gnss_driver::ReceiverAutoConfigOutputPort::kUsb},
             "USB-only preview should shrink to the documented USB message set");
  ctx.Expect(usb_text.find("Output port: usb") != std::string::npos &&
                 usb_text.find("output on USB") != std::string::npos &&
                 usb_text.find("UART1 baud rate") == std::string::npos,
             "USB-only preview text should decode USB message outputs and omit UART baud commands");

  ProfilePreviewOptions uart2_options;
  uart2_options.vendor = "ublox";
  uart2_options.profile = "rover_high_precision_debug";
  uart2_options.output_port = universal_gnss_driver::ReceiverAutoConfigOutputPort::kUart2;
  uart2_options.baud = 460800u;

  const auto uart2_result = BuildProfilePreview(uart2_options);
  const std::string uart2_text = FormatProfilePreviewText(uart2_result);

  ctx.Expect(uart2_result.status == ProfilePreviewStatus::kOk &&
                 uart2_result.summary.commands_total == 15u,
             "UART2-only diagnostics preview should include a UART2 baud command, rate command, "
             "nine UART2 message outputs, and four constellation toggles");
  ctx.Expect(uart2_text.find("Output port: uart2") != std::string::npos &&
                 uart2_text.find("set UART2 baud rate to 460800") != std::string::npos &&
                 uart2_text.find("output on UART2") != std::string::npos &&
                 uart2_text.find("output on USB") == std::string::npos,
             "UART2-only preview text should decode UART2 baud and message-output commands");
}

void TestFactoryResetPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "factory_reset";
  options.receiver_model = "UM981";

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk && result.commands.size() == 15u &&
                 result.summary.runtime_commands == 14u &&
                 result.summary.factory_reset_commands == 1u,
             "Unicore factory_reset preview should expose reset plus runtime recovery commands");
  ctx.Expect(
      !result.commands.empty() &&
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
  options.signal_profile = universal_gnss_driver::ReceiverAutoConfigSignalProfile::kAllSignals;
  options.rate_hz = 1.0;

  const auto result = BuildProfilePreview(options);
  const std::string json = FormatProfilePreviewJson(result, true);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk,
             "JSON formatting test setup should build successfully");
  ctx.Expect(json.find("\"vendor\": \"ublox\"") != std::string::npos &&
                 json.find("\"receiver_family\": \"F9/F10\"") != std::string::npos &&
                 json.find("\"profile\": \"rover_high_precision\"") != std::string::npos &&
                 json.find("\"signal_profile\": \"all_signals\"") != std::string::npos &&
                 json.find("\"summary\"") != std::string::npos,
             "JSON preview output should include metadata and summary objects");
  ctx.Expect(json.find("\"hex\": \"") != std::string::npos,
             "verbose JSON preview output should include binary hex when requested");
}

void TestUnicoreRuntimeTargetBaudPreview(TestContext& ctx)
{
  ProfilePreviewOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision";
  options.receiver_model = "UM981";
  options.baud = 921600u;

  const auto result = BuildProfilePreview(options);
  const std::string text = FormatProfilePreviewText(result);

  ctx.Expect(result.status == ProfilePreviewStatus::kOk &&
                 result.baud == std::optional<std::uint32_t>{921600u} &&
                 result.summary.commands_total == 14u && result.summary.runtime_commands == 14u &&
                 result.summary.persistent_commands == 0u &&
                 result.summary.factory_reset_commands == 0u,
             "runtime-only Unicore config baud overrides should preview successfully");
  ctx.Expect(
      text.find("Config baud override: 921600") != std::string::npos &&
          text.find("Target configured baud: 921600") != std::string::npos &&
          text.find("CONFIG COM1 921600 8 n 1") != std::string::npos &&
          text.find("command: FRESET") == std::string::npos &&
          text.find("SAVECONFIG") == std::string::npos,
      "runtime-only Unicore baud preview should include CONFIG COM1 without FRESET or SAVECONFIG");
  ctx.Expect(HasTextCommand(result, "CONFIG COM1 921600 8 n 1") &&
                 !HasTextCommand(result, "FRESET") && !HasTextCommand(result, "SAVECONFIG"),
             "runtime-only Unicore baud preview should emit only the live COM1 mutation");
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
  TestUbloxOutputPortPreview(ctx);
  TestFactoryResetPreview(ctx);
  TestJsonFormatting(ctx);
  TestUnicoreRuntimeTargetBaudPreview(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools profile preview tests passed\n";
  return EXIT_SUCCESS;
}
