#include <cstdlib>
#include <iostream>
#include <optional>
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

bool HasTextCommand(const universal_gnss_tools::ConfigPlanResult& result,
                    const std::string& command_text)
{
  for (const auto& command : result.commands)
  {
    if (command.command.payload.kind ==
            universal_gnss_driver::ReceiverCommandPayloadKind::kText &&
        command.command.payload.text.find(command_text) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

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
  ctx.Expect(result.summary.commands_total == 14u &&
                 result.summary.runtime_commands == 14u &&
                 result.summary.persistent_commands == 0u,
             "generic Unicore rover_high_precision_debug plans should report the expected safe command counts");
  ctx.Expect(text.find("MODE ROVER") != std::string::npos &&
                 text.find("UNLOG") != std::string::npos &&
                 text.find("LOG PVTSLNA ONTIME 0.2") != std::string::npos &&
                 text.find("model identity is unknown") != std::string::npos &&
                 !HasTextCommand(result, "CONFIG SIGNALGROUP"),
             "generic Unicore debug plans should skip CONFIG SIGNALGROUP and report the safe unknown-model fallback");

  options.receiver_model = "UM982";
  const auto um982_result = BuildConfigPlan(options);
  const std::string um982_text = FormatConfigPlanText(um982_result);
  ctx.Expect(um982_result.status == ConfigPlanStatus::kOk &&
                 um982_result.receiver_model == std::optional<std::string>{"UM982"} &&
                 um982_result.summary.commands_total == 15u &&
                 um982_text.find("CONFIG SIGNALGROUP 3 6") != std::string::npos,
             "UM982 config plans should expose the documented dual-antenna signal-group selection");
}

void TestUnicorePersistentTargetBaudPlan(TestContext& ctx)
{
  ConfigPlanOptions options;
  options.vendor = "unicore";
  options.profile = "rover_high_precision";
  options.receiver_model = "UM982";
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

void TestSignalProfilePlanning(TestContext& ctx)
{
  ConfigPlanOptions unicore_options;
  unicore_options.vendor = "unicore";
  unicore_options.profile = "rover_high_precision";
  unicore_options.receiver_model = "UM982";
  unicore_options.signal_profile =
      universal_gnss_driver::ReceiverAutoConfigSignalProfile::kMinimal;
  unicore_options.rate_hz = 1.0;

  const auto unicore_result = BuildConfigPlan(unicore_options);
  const std::string unicore_text = FormatConfigPlanText(unicore_result);

  ctx.Expect(unicore_result.status == ConfigPlanStatus::kOk &&
                 unicore_result.signal_profile ==
                     std::optional<universal_gnss_driver::ReceiverAutoConfigSignalProfile>{
                         universal_gnss_driver::ReceiverAutoConfigSignalProfile::kMinimal} &&
                 unicore_result.summary.commands_total == 12u,
             "Unicore minimal signal-profile plans should expose the reduced output plan");
  ctx.Expect(unicore_text.find("Signal profile override: minimal") != std::string::npos &&
                 unicore_text.find("BESTNAVA 1") != std::string::npos &&
                 unicore_text.find("GPGSV") == std::string::npos &&
                 unicore_text.find("PVTSLNA") == std::string::npos,
             "Unicore minimal signal-profile plan text should show the reduced runtime command set");

  ConfigPlanOptions unknown_unicore_options;
  unknown_unicore_options.vendor = "unicore";
  unknown_unicore_options.profile = "rover_high_precision";
  unknown_unicore_options.receiver_model = "UM952";
  unknown_unicore_options.signal_profile =
      universal_gnss_driver::ReceiverAutoConfigSignalProfile::kHighPrecision;
  const auto unknown_unicore_result = BuildConfigPlan(unknown_unicore_options);
  const std::string unknown_unicore_text = FormatConfigPlanText(unknown_unicore_result);
  ctx.Expect(unknown_unicore_result.status == ConfigPlanStatus::kOk &&
                 unknown_unicore_result.receiver_model ==
                     std::optional<std::string>{"UM952"} &&
                 unknown_unicore_result.summary.commands_total == 14u &&
                 unknown_unicore_text.find("Receiver model: UM952") != std::string::npos &&
                 unknown_unicore_text.find("safe generic non-baseline fallback") !=
                     std::string::npos &&
                 !HasTextCommand(unknown_unicore_result, "CONFIG SIGNALGROUP"),
             "unknown Unicore model plan text should report the safe fallback and skip CONFIG SIGNALGROUP");

  ConfigPlanOptions known_non_baseline_options;
  known_non_baseline_options.vendor = "unicore";
  known_non_baseline_options.profile = "rover_high_precision";
  known_non_baseline_options.receiver_model = "UM981";
  const auto known_non_baseline_result = BuildConfigPlan(known_non_baseline_options);
  const std::string known_non_baseline_text = FormatConfigPlanText(known_non_baseline_result);
  ctx.Expect(known_non_baseline_result.status == ConfigPlanStatus::kOk &&
                 known_non_baseline_result.receiver_model ==
                     std::optional<std::string>{"UM981"} &&
                 known_non_baseline_result.summary.commands_total == 14u &&
                 known_non_baseline_text.find("Receiver model: UM981") != std::string::npos &&
                 known_non_baseline_text.find("safe generic non-baseline fallback") ==
                     std::string::npos &&
                 !HasTextCommand(known_non_baseline_result, "CONFIG SIGNALGROUP"),
             "known non-baseline Unicore models such as UM981 should be accepted without falling back to the unknown-model path");

  ConfigPlanOptions nmea_options;
  nmea_options.vendor = "nmea";
  nmea_options.profile = "runtime_only";
  nmea_options.signal_profile =
      universal_gnss_driver::ReceiverAutoConfigSignalProfile::kBalanced;

  const auto nmea_result = BuildConfigPlan(nmea_options);
  ctx.Expect(nmea_result.status == ConfigPlanStatus::kOk &&
                 !nmea_result.warnings.empty() &&
                 nmea_result.warnings.front().find("signal_profile=balanced") !=
                     std::string::npos,
             "generic NMEA signal-profile planning should stay a warning-only no-op");
}

void TestUbloxOutputPortPlanning(TestContext& ctx)
{
  ConfigPlanOptions usb_options;
  usb_options.vendor = "ublox";
  usb_options.profile = "rover_high_precision";
  usb_options.output_port =
      universal_gnss_driver::ReceiverAutoConfigOutputPort::kUsb;
  usb_options.baud = 460800u;

  const auto usb_result = BuildConfigPlan(usb_options);
  const std::string usb_text = FormatConfigPlanText(usb_result);

  ctx.Expect(usb_result.status == ConfigPlanStatus::kOk &&
                 usb_result.summary.commands_total == 9u &&
                 usb_result.output_port ==
                     std::optional<universal_gnss_driver::ReceiverAutoConfigOutputPort>{
                         universal_gnss_driver::ReceiverAutoConfigOutputPort::kUsb} &&
                 usb_result.resolved_output_port ==
                     std::optional<universal_gnss_driver::ReceiverAutoConfigOutputPort>{
                         universal_gnss_driver::ReceiverAutoConfigOutputPort::kUsb},
             "USB-only config plans should shrink to the documented USB message set");
  ctx.Expect(usb_text.find("Output port: usb") != std::string::npos &&
                 usb_text.find("set UART1 baud rate") == std::string::npos &&
                 usb_text.find("set UART2 baud rate") == std::string::npos &&
                 usb_text.find("does not apply to USB") != std::string::npos,
             "USB-only config plan text should show the resolved USB port and warn that config-baud is ignored");

  ConfigPlanOptions uart1_options;
  uart1_options.vendor = "ublox";
  uart1_options.profile = "rover_high_precision";
  uart1_options.output_port =
      universal_gnss_driver::ReceiverAutoConfigOutputPort::kUart1;
  uart1_options.baud = 460800u;

  const auto uart1_result = BuildConfigPlan(uart1_options);
  const std::string uart1_text = FormatConfigPlanText(uart1_result);

  ctx.Expect(uart1_result.status == ConfigPlanStatus::kOk &&
                 uart1_result.summary.commands_total == 10u,
             "UART1-only config plans should include a single UART1 baud command");
  ctx.Expect(uart1_text.find("Output port: uart1") != std::string::npos &&
                 uart1_text.find("set UART1 baud rate to 460800") != std::string::npos &&
                 uart1_text.find("UART2 baud rate") == std::string::npos,
             "UART1-only config plan text should include the UART1 baud command only");

  ConfigPlanOptions all_options;
  all_options.vendor = "ublox";
  all_options.profile = "rover_high_precision";
  all_options.output_port =
      universal_gnss_driver::ReceiverAutoConfigOutputPort::kAll;

  const auto all_result = BuildConfigPlan(all_options);
  const std::string all_text = FormatConfigPlanText(all_result);

  ctx.Expect(all_result.status == ConfigPlanStatus::kOk &&
                 all_result.summary.commands_total == 17u &&
                 all_result.resolved_output_port ==
                     std::optional<universal_gnss_driver::ReceiverAutoConfigOutputPort>{
                         universal_gnss_driver::ReceiverAutoConfigOutputPort::kAll},
             "all-port config plans should emit USB, UART1, and UART2 message outputs");
  ctx.Expect(all_text.find("Output port: all") != std::string::npos &&
                 all_text.find("output on UART1") != std::string::npos &&
                 all_text.find("output on UART2") != std::string::npos &&
                 all_text.find("output on USB") != std::string::npos,
             "all-port config plan text should decode UART1, UART2, and USB message outputs");
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
                 result.summary.runtime_commands == 15u &&
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
  options.signal_profile =
      universal_gnss_driver::ReceiverAutoConfigSignalProfile::kAllSignals;
  options.rate_hz = 1.0;

  const auto result = BuildConfigPlan(options);
  const std::string json = FormatConfigPlanJson(result);

  ctx.Expect(result.status == ConfigPlanStatus::kOk,
             "JSON formatting test setup should build successfully");
  ctx.Expect(json.find("\"profile\": {") != std::string::npos &&
                 json.find("\"receiver_family\": \"F9/F10\"") != std::string::npos &&
                 json.find("\"signal_profile\": \"all_signals\"") != std::string::npos &&
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
  TestSignalProfilePlanning(ctx);
  TestUbloxOutputPortPlanning(ctx);
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
