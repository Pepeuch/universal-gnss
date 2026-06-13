#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss_driver/receiver_auto_config.hpp"

namespace
{

using universal_gnss_driver::BuildReceiverAutoConfigPlan;
using universal_gnss_driver::ParseReceiverAutoConfigProfile;
using universal_gnss_driver::ReceiverAutoConfigApplyMode;
using universal_gnss_driver::ReceiverAutoConfigPlanStatus;
using universal_gnss_driver::ReceiverAutoConfigProfile;
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
  ctx.Expect(std::string(
                 universal_gnss_driver::ToString(
                     ReceiverAutoConfigProfile::kRoverHighPrecisionDebug)) ==
                 "rover_high_precision_debug",
             "portable profile formatting should prefer the canonical generic profile names");
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
      MakeDiscoveryResult("/dev/ttyUSB0", 460800u, ReceiverDetectedFamily::kUnicore),
      ReceiverAutoConfigProfile::kRoverHighPrecision,
      ReceiverAutoConfigApplyMode::kPersistent,
      921600u);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 plan.request.config_baud == std::optional<std::uint32_t>{921600u} &&
                 !plan.commands.empty() &&
                 plan.commands[1].payload.text.find("CONFIG COM1 921600") != std::string::npos,
             "persistent Unicore planning should accept a baud override only through the clean reset workflow");
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
  TestUbloxFactoryResetStub(ctx);
  TestUnicoreRoverHighPrecisionPlans(ctx);
  TestUnicoreFactoryResetPlan(ctx);
  TestRuntimeOnlyPersistentModeRejected(ctx);
  TestPersistentApplyWarnings(ctx);
  TestUnicorePersistentBaudOverride(ctx);
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
