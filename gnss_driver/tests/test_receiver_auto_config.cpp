#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss_driver/receiver_auto_config.hpp"

namespace
{

using universal_gnss_driver::BuildReceiverAutoConfigPlan;
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

void TestUbloxRoverRuntimeOnlyPlan(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/serial/by-id/f9p", 921600u, ReceiverDetectedFamily::kUblox),
      ReceiverAutoConfigProfile::kRover,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk,
             "u-blox rover runtime-only planning should succeed");
  ctx.Expect(plan.vendor == universal_gnss_driver::ReceiverVendor::kUblox &&
                 plan.receiver_family_name == "F9/F10" &&
                 plan.detected_device == std::optional<std::string>("/dev/serial/by-id/f9p") &&
                 plan.detected_baud == std::optional<std::uint32_t>(921600u),
             "u-blox rover planning should preserve discovery context");
  ctx.Expect(plan.validation.receiver_recognized &&
                 plan.validation.config_supported &&
                 plan.validation.profile_supported &&
                 plan.validation.apply_mode_supported &&
                 plan.validation.production_ready &&
                 plan.validation.ready_to_execute,
             "u-blox rover planning should be recognized, supported, and ready to execute later");
  ctx.Expect(plan.validation.generated_command_count == 13u &&
                 plan.validation.runtime_command_count == 13u &&
                 plan.validation.persistent_command_count == 0u,
             "u-blox rover planning should report the expected runtime command counts");
  ctx.Expect(plan.rollback_expectation.changes_are_temporary &&
                 !plan.rollback_expectation.operator_action_required,
             "runtime-only u-blox planning should report temporary changes");
}

void TestUbloxDiagnosticsRuntimeOnlyPlan(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyACM0", 921600u, ReceiverDetectedFamily::kUblox),
      ReceiverAutoConfigProfile::kDiagnostics,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 plan.validation.generated_command_count == 23u &&
                 plan.validation.runtime_command_count == 23u,
             "u-blox diagnostics runtime-only planning should generate the expected command counts");
}

void TestUnicoreRoverRuntimeOnlyPlan(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore),
      ReceiverAutoConfigProfile::kRover,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk,
             "Unicore rover runtime-only planning should succeed");
  ctx.Expect(plan.vendor == universal_gnss_driver::ReceiverVendor::kUnicore &&
                 plan.receiver_family_name == "UM98x" &&
                 plan.validation.generated_command_count == 10u &&
                 plan.validation.runtime_command_count == 10u,
             "Unicore rover planning should generate the expected runtime command counts");
}

void TestNmeaRejectedCleanly(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB9", 115200u, ReceiverDetectedFamily::kNmea),
      ReceiverAutoConfigProfile::kRover,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kUnsupportedReceiver &&
                 plan.capabilities_known &&
                 !plan.validation.config_supported &&
                 !plan.unsupported_reason.empty(),
             "generic NMEA planning should be rejected cleanly as unsupported");
}

void TestBaseMarkedNotProductionReady(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyACM0", 921600u, ReceiverDetectedFamily::kUblox),
      ReceiverAutoConfigProfile::kBase,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 plan.validation.generated_command_count == 11u,
             "u-blox base planning should still build a command sequence");
  ctx.Expect(!plan.validation.production_ready &&
                 !plan.validation.ready_to_execute &&
                 ContainsWarning(plan, "not yet production-ready"),
             "base planning should be marked not production-ready");
}

void TestPersistentApplyWarnings(TestContext& ctx)
{
  const auto plan = BuildReceiverAutoConfigPlan(
      MakeDiscoveryResult("/dev/ttyUSB0", 921600u, ReceiverDetectedFamily::kUnicore),
      ReceiverAutoConfigProfile::kRover,
      ReceiverAutoConfigApplyMode::kPersistent);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kOk &&
                 plan.validation.generated_command_count == 11u &&
                 plan.validation.persistent_command_count == 1u,
             "persistent Unicore planning should include SAVECONFIG");
  ctx.Expect(ContainsWarning(plan, "persistent apply") &&
                 ContainsWarning(plan, "SAVECONFIG") &&
                 plan.rollback_expectation.operator_action_required,
             "persistent planning should surface explicit warnings and manual rollback expectations");
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
      ReceiverAutoConfigProfile::kRover,
      ReceiverAutoConfigApplyMode::kRuntimeOnly);

  ctx.Expect(plan.status == ReceiverAutoConfigPlanStatus::kUnsupportedReceiver &&
                 plan.unsupported_reason == "no_data",
             "unknown receiver planning should be rejected with the discovery reason");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestUbloxRoverRuntimeOnlyPlan(ctx);
  TestUbloxDiagnosticsRuntimeOnlyPlan(ctx);
  TestUnicoreRoverRuntimeOnlyPlan(ctx);
  TestNmeaRejectedCleanly(ctx);
  TestBaseMarkedNotProductionReady(ctx);
  TestPersistentApplyWarnings(ctx);
  TestUnknownReceiverRejected(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver auto config tests passed\n";
  return EXIT_SUCCESS;
}
