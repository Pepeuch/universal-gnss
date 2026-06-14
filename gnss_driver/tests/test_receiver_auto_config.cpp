#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss_driver/receiver_auto_config.hpp"

namespace
{

using universal_gnss_driver::BuildReceiverAutoConfigPlan;
using universal_gnss_driver::ParseReceiverAutoConfigProfile;
using universal_gnss_driver::ParseReceiverAutoConfigOutputPort;
using universal_gnss_driver::ParseReceiverAutoConfigSignalProfile;
using universal_gnss_driver::ReceiverAutoConfigApplyMode;
using universal_gnss_driver::ReceiverAutoConfigOutputPort;
using universal_gnss_driver::ReceiverAutoConfigPlanStatus;
using universal_gnss_driver::ReceiverAutoConfigProfile;
using universal_gnss_driver::ReceiverAutoConfigRequest;
using universal_gnss_driver::ReceiverAutoConfigSignalProfile;
using universal_gnss_driver::ReceiverDetectedFamily;
using universal_gnss_driver::ReceiverPortSource;
using universal_gnss_driver::ReceiverProbeConfidence;
using universal_gnss_driver::ReceiverProbeResult;
using universal_gnss_driver::ReceiverTransportType;

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
                                  : "unknown";
  return result;
}

bool ContainsWarning(const universal_gnss_driver::ReceiverAutoConfigPlan& plan,
                     const std::string& needle)
{
  for (const auto& warning : plan.warnings)
  {
    if (warning.find(needle) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

bool ContainsCommandText(const universal_gnss_driver::ReceiverAutoConfigPlan& plan,
                         const std::string& needle)
{
  for (const auto& command : plan.commands)
  {
    if (command.payload.text.find(needle) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

void TestProfileParsingAndFormatting(TestContext& ctx)
{
  ctx.Expect(ParseReceiverAutoConfigProfile("runtime_only") ==
                 std::optional<ReceiverAutoConfigProfile>{
                     ReceiverAutoConfigProfile::kRuntimeOnly},
             "runtime_only should parse to the new no-op portable profile");
  ctx.Expect(ParseReceiverAutoConfigProfile("rover") ==
                 std::optional<ReceiverAutoConfigProfile>{
                     ReceiverAutoConfigProfile::kRoverHighPrecision},
             "legacy rover alias should map to rover_high_precision");
  ctx.Expect(ParseReceiverAutoConfigProfile("diagnostics") ==
                 std::optional<ReceiverAutoConfigProfile>{
                     ReceiverAutoConfigProfile::kRoverHighPrecisionDebug},
             "legacy diagnostics alias should map to rover_high_precision_debug");
  ctx.Expect(ParseReceiverAutoConfigProfile("factory-reset") ==
                 std::optional<ReceiverAutoConfigProfile>{
                     ReceiverAutoConfigProfile::kFactoryReset},
             "factory-reset should parse as a supported portable alias");
  ctx.Expect(ParseReceiverAutoConfigSignalProfile("high-precision") ==
                 std::optional<ReceiverAutoConfigSignalProfile>{
                     ReceiverAutoConfigSignalProfile::kHighPrecision},
             "high-precision should parse as the canonical generic signal-profile alias");
  ctx.Expect(ParseReceiverAutoConfigSignalProfile("low_bandwidth") ==
                 std::optional<ReceiverAutoConfigSignalProfile>{
                     ReceiverAutoConfigSignalProfile::kMinimal},
             "low_bandwidth should remain accepted as a compatibility alias for minimal");
  ctx.Expect(ParseReceiverAutoConfigOutputPort("usb") ==
                 std::optional<ReceiverAutoConfigOutputPort>{
                     ReceiverAutoConfigOutputPort::kUsb},
             "usb should parse as the canonical u-blox output-port selector");
  ctx.Expect(ParseReceiverAutoConfigOutputPort("auto") ==
                 std::optional<ReceiverAutoConfigOutputPort>{
                     ReceiverAutoConfigOutputPort::kAuto},
             "auto should parse as the transport-aware u-blox output-port selector");
  ctx.Expect(std::string(
                 universal_gnss_driver::ToString(
                     ReceiverAutoConfigProfile::kRoverHighPrecisionDebug)) ==
                 "rover_high_precision_debug",
             "portable profile formatting should prefer the canonical generic profile names");
  ctx.Expect(std::string(
                 universal_gnss_driver::ToString(
                     ReceiverAutoConfigSignalProfile::kAllSignals)) ==
                 "all_signals",
             "portable signal-profile formatting should prefer canonical vendor-neutral names");
  ctx.Expect(std::string(
                 universal_gnss_driver::ToString(
                     ReceiverAutoConfigOutputPort::kUart2)) ==
                 "uart2",
             "portable output-port formatting should prefer canonical u-blox interface names");
}

void TestUbloxRuntimeOnlyPlan(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/serial/by-id/f9p", 921600u, ReceiverDetectedFamily::kUblox),
      ReceiverAutoConfigProfile::kRuntimeOnly,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk,
             "u-blox runtime_only planning should succeed");
  ctx.Expect(plan.validation.generated_command_count == 0u &&
                 plan.validation.runtime_command_count == 0u &&
                 plan.validation.persistent_command_count == 0u &&
                 plan.validation.production_ready &&
                 plan.validation.ready_to_execute,
             "u-blox runtime_only planning should remain a supported no-op plan");
  ctx.Expect(plan.rollback_expectation.summary ==
                 "no receiver configuration changes are planned",
             "runtime_only planning should report an explicit no-change rollback summary");
}

void TestUbloxRoverHighPrecisionPlans(TestContext& ctx)
{
  const auto rover_plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/serial/by-id/f9p", 921600u, ReceiverDetectedFamily::kUblox),
      ReceiverAutoConfigProfile::kRoverHighPrecision,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);
  const auto debug_plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyACM0", 921600u, ReceiverDetectedFamily::kUblox),
      ReceiverAutoConfigProfile::kRoverHighPrecisionDebug,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(rover_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 rover_plan.validation.generated_command_count == 13u &&
                 rover_plan.validation.runtime_command_count == 13u,
             "u-blox rover_high_precision planning should preserve the validated rover command set");
  ctx.Expect(debug_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 debug_plan.validation.generated_command_count == 23u &&
                 debug_plan.validation.runtime_command_count == 23u,
             "u-blox rover_high_precision_debug planning should preserve the existing diagnostics builder");
}

void TestUbloxOutputPortPlanning(TestContext& ctx)
{
  ReceiverAutoConfigRequest request;
  request.receiver_family = ReceiverDetectedFamily::kUblox;
  request.discovery_result =
      MakeDiscoveryResult("/dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00",
                          921600u,
                          ReceiverDetectedFamily::kUblox);
  request.transport_device_path = request.discovery_result->path;
  request.requested_profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  request.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;

  const auto legacy_plan = BuildReceiverAutoConfigPlan(request);
  ctx.Expect(legacy_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 legacy_plan.validation.generated_command_count == 13u &&
                 ContainsWarning(legacy_plan, "legacy default output-port set"),
             "u-blox plans without an explicit output port should keep the legacy UART1+USB command set and warn on USB-looking transports");

  request.output_port = ReceiverAutoConfigOutputPort::kUsb;
  request.config_baud = 460800u;
  const auto usb_plan = BuildReceiverAutoConfigPlan(request);
  ctx.Expect(usb_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 usb_plan.validation.generated_command_count == 9u &&
                 usb_plan.resolved_output_port ==
                     std::optional<ReceiverAutoConfigOutputPort>{
                         ReceiverAutoConfigOutputPort::kUsb} &&
                 ContainsWarning(usb_plan, "does not apply to USB"),
             "u-blox USB-only plans should drop UART baud commands and warn that config-baud is ignored on USB");

  request.output_port = ReceiverAutoConfigOutputPort::kAll;
  request.config_baud = 460800u;
  const auto all_plan = BuildReceiverAutoConfigPlan(request);
  ctx.Expect(all_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 all_plan.validation.generated_command_count == 19u &&
                 all_plan.resolved_output_port ==
                     std::optional<ReceiverAutoConfigOutputPort>{
                         ReceiverAutoConfigOutputPort::kAll} &&
                 ContainsWarning(all_plan, "both UART1 and UART2"),
             "u-blox all-port plans should expand to USB, UART1, and UART2 outputs plus both UART baud commands when config-baud is requested");

  request.output_port = ReceiverAutoConfigOutputPort::kAuto;
  request.config_baud = std::nullopt;
  const auto auto_usb_plan = BuildReceiverAutoConfigPlan(request);
  ctx.Expect(auto_usb_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 auto_usb_plan.validation.generated_command_count == 9u &&
                 auto_usb_plan.resolved_output_port ==
                     std::optional<ReceiverAutoConfigOutputPort>{
                         ReceiverAutoConfigOutputPort::kUsb} &&
                 ContainsWarning(auto_usb_plan, "resolved to usb"),
             "u-blox auto output-port plans should resolve USB-attached receivers to USB output keys");

  request.discovery_result =
      MakeDiscoveryResult("/dev/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00",
                          921600u,
                          ReceiverDetectedFamily::kUblox);
  request.transport_device_path = request.discovery_result->path;
  const auto auto_usb_alias_plan = BuildReceiverAutoConfigPlan(request);
  ctx.Expect(auto_usb_alias_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 auto_usb_alias_plan.validation.generated_command_count == 9u &&
                 auto_usb_alias_plan.resolved_output_port ==
                     std::optional<ReceiverAutoConfigOutputPort>{
                         ReceiverAutoConfigOutputPort::kUsb} &&
                 ContainsWarning(auto_usb_alias_plan, "resolved to usb"),
             "u-blox auto output-port plans should also treat /dev/usb-u-blox aliases as USB transports");

  request.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUblox);
  request.transport_device_path = request.discovery_result->path;
  request.output_port = ReceiverAutoConfigOutputPort::kAuto;
  const auto auto_uart_plan = BuildReceiverAutoConfigPlan(request);
  ctx.Expect(auto_uart_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 auto_uart_plan.validation.generated_command_count == 9u &&
                 auto_uart_plan.resolved_output_port ==
                     std::optional<ReceiverAutoConfigOutputPort>{
                         ReceiverAutoConfigOutputPort::kUart1} &&
                 ContainsWarning(auto_uart_plan, "resolved to uart1"),
             "u-blox auto output-port plans should resolve ttyUSB transports to UART1 output keys");
}

void TestUbloxFactoryResetStub(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/serial/by-id/f9p", 921600u, ReceiverDetectedFamily::kUblox),
      ReceiverAutoConfigProfile::kFactoryReset,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kUnsupportedProfile &&
                 !plan.validation.profile_supported &&
                 plan.error_message.find("factory_reset") != std::string::npos,
             "u-blox factory_reset should stay an explicit unsupported portable stub for now");
}

void TestUnicoreRoverHighPrecisionPlans(TestContext& ctx)
{
  const auto rover_plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore),
      ReceiverAutoConfigProfile::kRoverHighPrecision,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);
  const auto debug_plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore),
      ReceiverAutoConfigProfile::kRoverHighPrecisionDebug,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(rover_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 rover_plan.validation.generated_command_count == 15u &&
                 rover_plan.validation.runtime_command_count == 15u,
             "Unicore rover_high_precision planning should preserve the validated runtime profile");
  ctx.Expect(debug_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 debug_plan.validation.generated_command_count == 15u &&
                 debug_plan.validation.runtime_command_count == 15u,
             "Unicore rover_high_precision_debug planning should keep the same lean command count");
  ctx.Expect(rover_plan.commands[10].payload.text.find("LOG PVTSLNA ONTIME 1") != std::string::npos &&
                 debug_plan.commands[10].payload.text.find("LOG PVTSLNA ONTIME 0.2") !=
                     std::string::npos,
             "Unicore debug planning should keep PVTSLNA at 5 Hz while the normal rover profile stays at 1 Hz");
}

void TestSignalProfileCapabilityMapping(TestContext& ctx)
{
  ReceiverAutoConfigRequest unicore_request;
  unicore_request.receiver_family = ReceiverDetectedFamily::kUnicore;
  unicore_request.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore);
  unicore_request.requested_profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  unicore_request.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  unicore_request.signal_profile = ReceiverAutoConfigSignalProfile::kHighPrecision;
  unicore_request.rate_hz = 5.0;

  const auto unicore_high_precision_plan = BuildReceiverAutoConfigPlan(unicore_request);
  ctx.Expect(unicore_high_precision_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 ContainsCommandText(unicore_high_precision_plan, "CONFIG SIGNALGROUP 3 6") &&
                 ContainsCommandText(unicore_high_precision_plan, "BESTNAVA 0.2") &&
                 ContainsCommandText(unicore_high_precision_plan, "LOG GPGGA ONTIME 1") &&
                 ContainsCommandText(unicore_high_precision_plan, "LOG PVTSLNA ONTIME 1") &&
                 ContainsCommandText(unicore_high_precision_plan, "RTKSTATUSA 1"),
             "Unicore high_precision signal-profile planning should map to CONFIG SIGNALGROUP 3 6 while keeping auxiliary logs at their safe default rates");

  unicore_request.signal_profile = ReceiverAutoConfigSignalProfile::kMinimal;
  unicore_request.rate_hz = 1.0;
  const auto unicore_minimal_plan = BuildReceiverAutoConfigPlan(unicore_request);
  ctx.Expect(unicore_minimal_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 unicore_minimal_plan.validation.generated_command_count == 12u &&
                 ContainsCommandText(unicore_minimal_plan, "BESTNAVA 1") &&
                 !ContainsCommandText(unicore_minimal_plan, "GPGSV") &&
                 !ContainsCommandText(unicore_minimal_plan, "GPGST") &&
                 !ContainsCommandText(unicore_minimal_plan, "PVTSLNA") &&
                 ContainsWarning(unicore_minimal_plan, "signal_profile=minimal"),
             "Unicore minimal signal-profile planning should reduce the auxiliary output set while preserving a 1 Hz BESTNAVA runtime plan");

  unicore_request.signal_profile = ReceiverAutoConfigSignalProfile::kBalanced;
  unicore_request.rate_hz = 10.0;
  const auto unicore_fast_plan = BuildReceiverAutoConfigPlan(unicore_request);
  ctx.Expect(unicore_fast_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 ContainsCommandText(unicore_fast_plan, "BESTNAVA 0.1"),
             "Unicore rate-hz planning should map 10 Hz requests to a 0.1 s BESTNAVA period");

  ReceiverAutoConfigRequest ublox_request;
  ublox_request.receiver_family = ReceiverDetectedFamily::kUblox;
  ublox_request.discovery_result =
      MakeDiscoveryResult("/dev/serial/by-id/f9p", 921600u, ReceiverDetectedFamily::kUblox);
  ublox_request.requested_profile = ReceiverAutoConfigProfile::kRoverHighPrecision;
  ublox_request.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  ublox_request.signal_profile = ReceiverAutoConfigSignalProfile::kAllSignals;

  const auto ublox_plan = BuildReceiverAutoConfigPlan(ublox_request);
  ctx.Expect(ublox_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 ublox_plan.validation.generated_command_count == 13u &&
                 ContainsWarning(ublox_plan, "signal_profile=all_signals"),
             "u-blox signal-profile requests without a documented portable translation should stay honest and warn while keeping the standard plan");
}

void TestUnicoreFactoryResetPlan(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore),
      ReceiverAutoConfigProfile::kFactoryReset,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 plan.validation.generated_command_count == 17u &&
                 plan.validation.runtime_command_count == 16u &&
                 plan.validation.factory_reset_command_count == 1u,
             "Unicore factory_reset planning should expand into reset plus runtime recovery commands");
  ctx.Expect(plan.validation.production_ready &&
                 plan.validation.ready_to_execute &&
                 ContainsWarning(plan, "115200") &&
                 ContainsWarning(plan, "reconnect/probe") &&
                 ContainsWarning(plan, "30 seconds"),
             "Unicore factory_reset planning should document the reset recovery workflow and restart delay");
}

void TestRuntimeOnlyPersistentModeRejected(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore),
      ReceiverAutoConfigProfile::kRuntimeOnly,
      ReceiverAutoConfigApplyMode::kPersistent);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kUnsupportedApplyMode &&
                 !plan.validation.apply_mode_supported,
             "runtime_only profiles should reject persistent apply requests cleanly");
}

void TestPersistentApplyWarnings(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore),
      ReceiverAutoConfigProfile::kRoverHighPrecision,
      ReceiverAutoConfigApplyMode::kPersistent);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 plan.validation.generated_command_count == 18u &&
                 plan.validation.runtime_command_count == 16u &&
                 plan.validation.persistent_command_count == 1u &&
                 plan.validation.factory_reset_command_count == 1u,
             "persistent Unicore rover_high_precision planning should rebuild the saved profile from a clean reset baseline");
  ctx.Expect(ContainsWarning(plan, "FRESET") &&
                 ContainsWarning(plan, "SAVECONFIG") &&
                 ContainsWarning(plan, "clean baseline") &&
                 plan.rollback_expectation.operator_action_required,
             "persistent portable planning should surface reset-first warnings and manual rollback expectations");
}

void TestUnicorePersistentBaudOverride(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore),
      ReceiverAutoConfigProfile::kRoverHighPrecision,
      ReceiverAutoConfigApplyMode::kPersistent,
      460800u);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 plan.request.config_baud == std::optional<std::uint32_t>{460800u} &&
                 !plan.commands.empty() &&
                 plan.commands[1].payload.text.find("CONFIG COM1 460800") != std::string::npos,
             "persistent Unicore planning should accept a baud override only through the clean reset workflow");
}

void TestUnicorePersistentDefaultTargetBaud(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB0", 460800u, ReceiverDetectedFamily::kUnicore),
      ReceiverAutoConfigProfile::kRoverHighPrecision,
      ReceiverAutoConfigApplyMode::kPersistent);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 !plan.request.config_baud.has_value() &&
                 !plan.commands.empty() &&
                 plan.commands[1].payload.text.find("CONFIG COM1 921600") != std::string::npos,
             "persistent Unicore planning should default the post-reset target baud to 921600 when no override is provided");
}

void TestNmeaProfiles(TestContext& ctx)
{
  const auto runtime_only_plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB9", 115200u, ReceiverDetectedFamily::kNmea),
      ReceiverAutoConfigProfile::kRuntimeOnly,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);
  const auto config_plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB9", 115200u, ReceiverDetectedFamily::kNmea),
      ReceiverAutoConfigProfile::kRoverHighPrecision,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(runtime_only_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 runtime_only_plan.validation.generated_command_count == 0u,
             "generic NMEA receivers should support the read-only runtime_only portable profile");
  ctx.Expect(config_plan.status == ReceiverAutoConfigPlanStatus::kUnsupportedProfile &&
                 !config_plan.validation.profile_supported &&
                 config_plan.error_message.find("runtime_only") != std::string::npos,
             "generic NMEA receivers should reject write-side portable profiles with a clear reason");

  ReceiverAutoConfigRequest signal_request;
  signal_request.receiver_family = ReceiverDetectedFamily::kNmea;
  signal_request.discovery_result =
      MakeDiscoveryResult("/dev/ttyUSB9", 115200u, ReceiverDetectedFamily::kNmea);
  signal_request.requested_profile = ReceiverAutoConfigProfile::kRuntimeOnly;
  signal_request.apply_mode = ReceiverAutoConfigApplyMode::kRuntimeOnly;
  signal_request.signal_profile = ReceiverAutoConfigSignalProfile::kBalanced;

  const auto signal_plan = BuildReceiverAutoConfigPlan(signal_request);
  ctx.Expect(signal_plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 signal_plan.validation.generated_command_count == 0u &&
                 ContainsWarning(signal_plan, "signal_profile=balanced"),
             "generic NMEA signal-profile requests should remain a warning-only no-op under runtime_only");
}

void TestUnknownReceiverRejected(TestContext& ctx)
{
  ReceiverProbeResult unknown;
  unknown.path = "/dev/ttyUSB99";
  unknown.selected_baud = 9600u;
  unknown.detected_family = ReceiverDetectedFamily::kUnknown;
  unknown.confidence = ReceiverProbeConfidence::kNone;
  unknown.discovery_score = 0;
  unknown.reason = "no_data";

  const auto plan = BuildReceiverAutoConfigPlan(
      unknown,
      ReceiverAutoConfigProfile::kRoverHighPrecision,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kUnsupportedReceiver &&
                 plan.unsupported_reason == "no_data",
             "unknown receiver planning should still be rejected with the discovery reason");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestProfileParsingAndFormatting(ctx);
  TestUbloxRuntimeOnlyPlan(ctx);
  TestUbloxRoverHighPrecisionPlans(ctx);
  TestUbloxOutputPortPlanning(ctx);
  TestUbloxFactoryResetStub(ctx);
  TestUnicoreRoverHighPrecisionPlans(ctx);
  TestSignalProfileCapabilityMapping(ctx);
  TestUnicoreFactoryResetPlan(ctx);
  TestRuntimeOnlyPersistentModeRejected(ctx);
  TestPersistentApplyWarnings(ctx);
  TestUnicorePersistentBaudOverride(ctx);
  TestUnicorePersistentDefaultTargetBaud(ctx);
  TestNmeaProfiles(ctx);
  TestUnknownReceiverRejected(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver auto config tests passed\n";
  return EXIT_SUCCESS;
}
