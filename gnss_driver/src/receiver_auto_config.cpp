#include "universal_gnss_driver/receiver_auto_config.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "universal_gnss_driver/nmea_driver.hpp"
#include "universal_gnss_driver/receiver_driver.hpp"
#include "universal_gnss_driver/ublox_config_profile_builder.hpp"
#include "universal_gnss_driver/ublox_driver.hpp"
#include "universal_gnss_driver/unicore_config_profile_builder.hpp"
#include "universal_gnss_driver/unicore_driver.hpp"
#include "universal_gnss_protocols/ubx_cfg_builder.hpp"

namespace universal_gnss_driver
{

namespace
{

using universal_gnss_protocols::UbxCfgLayer;

ReceiverCommandSafetyLevel ToSafetyLevel(const ReceiverAutoConfigApplyMode apply_mode)
{
  return apply_mode == ReceiverAutoConfigApplyMode::kPersistent
             ? ReceiverCommandSafetyLevel::kPersistent
             : ReceiverCommandSafetyLevel::kRuntime;
}

ReceiverAutoConfigPlan MakeBasePlan(const ReceiverAutoConfigRequest& request)
{
  ReceiverAutoConfigPlan plan;
  plan.request = request;
  return plan;
}

ReceiverAutoConfigPlan MakeErrorPlan(const ReceiverAutoConfigRequest& request,
                                     const ReceiverAutoConfigPlanStatus status,
                                     const std::string& message)
{
  ReceiverAutoConfigPlan plan = MakeBasePlan(request);
  plan.status = status;
  plan.error_message = message;
  return plan;
}

void CopyDiscoveryContext(ReceiverAutoConfigPlan& plan)
{
  if (!plan.request.discovery_result.has_value())
  {
    return;
  }

  const auto& discovery = *plan.request.discovery_result;
  plan.detected_device = discovery.path;
  plan.detected_stable_id = discovery.stable_id;
  plan.detected_baud = discovery.selected_baud;
  plan.discovery_confidence = discovery.confidence;
  plan.discovery_score = discovery.discovery_score;
}

void SummarizeCommands(ReceiverAutoConfigPlan& plan)
{
  plan.validation.generated_command_count = plan.commands.size();

  for (const auto& command : plan.commands)
  {
    switch (command.safety_level)
    {
      case ReceiverCommandSafetyLevel::kRuntime:
        ++plan.validation.runtime_command_count;
        break;
      case ReceiverCommandSafetyLevel::kPersistent:
        ++plan.validation.persistent_command_count;
        break;
      case ReceiverCommandSafetyLevel::kFactoryReset:
        ++plan.validation.factory_reset_command_count;
        break;
    }
  }
}

void ApplyPersistentWarningsAndRollback(ReceiverAutoConfigPlan& plan)
{
  plan.warnings.push_back(
      "persistent apply modifies non-volatile receiver state and should only be "
      "executed with explicit operator confirmation");
  plan.rollback_expectation.changes_are_temporary = false;
  plan.rollback_expectation.operator_action_required = true;
  plan.rollback_expectation.summary =
      "persistent configuration changes are not auto-rolled back";
  plan.rollback_expectation.operator_action =
      "reapply a known-good profile or use vendor tooling to restore the desired state";

  if (plan.request.receiver_family == ReceiverDetectedFamily::kUblox)
  {
    plan.warnings.push_back(
        "u-blox persistent planning currently targets CFG-RAM plus CFG-BBR");
  }
  else if (plan.request.receiver_family == ReceiverDetectedFamily::kUnicore)
  {
    plan.warnings.push_back(
        "Unicore persistent planning currently relies on SAVECONFIG");
  }
}

void ApplyRuntimeRollback(ReceiverAutoConfigPlan& plan)
{
  plan.rollback_expectation.changes_are_temporary = true;
  plan.rollback_expectation.operator_action_required = false;

  if (plan.request.receiver_family == ReceiverDetectedFamily::kUblox)
  {
    plan.rollback_expectation.summary =
        "runtime-only u-blox changes are planned against CFG-RAM only";
    plan.rollback_expectation.operator_action =
        "reboot the receiver to clear temporary CFG-RAM changes";
    return;
  }

  if (plan.request.receiver_family == ReceiverDetectedFamily::kUnicore)
  {
    plan.rollback_expectation.summary =
        "runtime-only Unicore planning does not emit SAVECONFIG";
    plan.rollback_expectation.operator_action =
        "restart the receiver or reapply a known-good runtime profile if temporary changes need to be cleared";
    return;
  }

  plan.rollback_expectation.summary =
      "runtime-only plans should remain temporary";
  plan.rollback_expectation.operator_action =
      "reconnect or restart the receiver if temporary changes need to be cleared";
}

void ApplyBaseProfileWarning(ReceiverAutoConfigPlan& plan)
{
  plan.validation.production_ready = false;
  plan.validation.ready_to_execute = false;
  plan.warnings.push_back(
      "base profile planning is not yet production-ready; survey-in and full "
      "base-station orchestration remain deferred");
}

bool ValidateRateHz(const ReceiverAutoConfigRequest& request,
                    ReceiverAutoConfigPlan& plan)
{
  if (!request.rate_hz.has_value())
  {
    return true;
  }

  if (!(*request.rate_hz > 0.0) || !std::isfinite(*request.rate_hz))
  {
    plan.status = ReceiverAutoConfigPlanStatus::kInvalidArgument;
    plan.error_message = "rate-hz must be positive";
    return false;
  }

  return true;
}

bool ValidateConfigBaud(const ReceiverAutoConfigRequest& request,
                        ReceiverAutoConfigPlan& plan)
{
  if (!request.config_baud.has_value())
  {
    return true;
  }

  if (*request.config_baud == 0u)
  {
    plan.status = ReceiverAutoConfigPlanStatus::kInvalidArgument;
    plan.error_message = "config baud must be non-zero";
    return false;
  }

  return true;
}

ReceiverAutoConfigPlan BuildUbloxPlan(const ReceiverAutoConfigRequest& request)
{
  ReceiverAutoConfigPlan plan = MakeBasePlan(request);
  plan.vendor = ReceiverVendor::kUblox;
  plan.receiver_family_name = "F9/F10";
  plan.capabilities_known = true;
  plan.capabilities = UbloxDriver{}.capabilities();
  plan.validation.receiver_recognized = true;
  plan.validation.config_supported = true;
  plan.validation.apply_mode_supported = true;
  plan.validation.profile_supported = true;

  if (!ValidateConfigBaud(request, plan) || !ValidateRateHz(request, plan))
  {
    return plan;
  }

  const auto safety_level = ToSafetyLevel(request.apply_mode);
  std::vector<UbxCfgLayer> layers{
      UbxCfgLayer::kRam,
  };
  if (safety_level == ReceiverCommandSafetyLevel::kPersistent)
  {
    layers.push_back(UbxCfgLayer::kBbr);
  }

  UbloxConfigProfile profile;
  switch (request.requested_profile)
  {
    case ReceiverAutoConfigProfile::kRover:
      profile = UbloxConfigProfileBuilder::BuildUbloxRoverProfile(safety_level, layers);
      break;
    case ReceiverAutoConfigProfile::kBase:
      profile = UbloxConfigProfileBuilder::BuildUbloxBaseProfile(safety_level, layers);
      break;
    case ReceiverAutoConfigProfile::kDiagnostics:
      profile = UbloxConfigProfileBuilder::BuildUbloxDiagnosticsProfile(safety_level, layers);
      break;
  }

  if (request.config_baud.has_value())
  {
    profile.port.uart1_baudrate = *request.config_baud;
  }
  if (request.rate_hz.has_value())
  {
    profile.measurement_rate_hz = *request.rate_hz;
  }

  const auto build_result = UbloxConfigProfileBuilder::Build(profile);
  if (build_result.status != UbloxConfigProfileBuildStatus::kOk)
  {
    plan.status = ReceiverAutoConfigPlanStatus::kBuildError;
    plan.error_message = build_result.error_message;
    return plan;
  }

  plan.commands = build_result.commands;
  SummarizeCommands(plan);
  plan.validation.production_ready = true;
  plan.validation.ready_to_execute =
      request.apply_mode != ReceiverAutoConfigApplyMode::kDryRun;

  if (request.requested_profile == ReceiverAutoConfigProfile::kBase)
  {
    ApplyBaseProfileWarning(plan);
  }

  if (request.apply_mode == ReceiverAutoConfigApplyMode::kPersistent)
  {
    ApplyPersistentWarningsAndRollback(plan);
  }
  else
  {
    ApplyRuntimeRollback(plan);
  }

  return plan;
}

bool IsPeriodicUnicoreMessage(const UnicoreOutputMessageKind message)
{
  return message != UnicoreOutputMessageKind::kRtcmstatusa;
}

ReceiverAutoConfigPlan BuildUnicorePlan(const ReceiverAutoConfigRequest& request)
{
  ReceiverAutoConfigPlan plan = MakeBasePlan(request);
  plan.vendor = ReceiverVendor::kUnicore;
  plan.receiver_family_name = "UM98x";
  plan.capabilities_known = true;
  plan.capabilities = UnicoreDriver{}.capabilities();
  plan.validation.receiver_recognized = true;
  plan.validation.config_supported = true;
  plan.validation.apply_mode_supported = true;

  if (!ValidateRateHz(request, plan))
  {
    return plan;
  }

  if (request.config_baud.has_value())
  {
    plan.status = ReceiverAutoConfigPlanStatus::kInvalidArgument;
    plan.error_message =
        "Unicore auto-configuration planning does not support baud overrides because the portable builder does not emit baud commands";
    return plan;
  }

  if (request.requested_profile == ReceiverAutoConfigProfile::kBase)
  {
    plan.status = ReceiverAutoConfigPlanStatus::kUnsupportedProfile;
    plan.validation.profile_supported = false;
    plan.error_message =
        "Unicore base profile planning is not yet supported by the portable config layer";
    plan.unsupported_reason = plan.error_message;
    ApplyBaseProfileWarning(plan);
    ApplyRuntimeRollback(plan);
    return plan;
  }

  plan.validation.profile_supported = true;

  const auto persistence = request.apply_mode == ReceiverAutoConfigApplyMode::kPersistent
                               ? UnicorePersistenceTarget::kSaveConfig
                               : UnicorePersistenceTarget::kRuntimeOnly;

  UnicoreConfigProfile profile;
  switch (request.requested_profile)
  {
    case ReceiverAutoConfigProfile::kRover:
      profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(persistence);
      break;
    case ReceiverAutoConfigProfile::kDiagnostics:
      profile = UnicoreConfigProfileBuilder::BuildUnicoreDiagnosticsProfile(persistence);
      break;
    case ReceiverAutoConfigProfile::kBase:
      break;
  }

  if (request.rate_hz.has_value())
  {
    const double period_s = 1.0 / *request.rate_hz;
    for (auto& output : profile.output_messages)
    {
      if (IsPeriodicUnicoreMessage(output.message))
      {
        output.period_s = period_s;
      }
    }
  }

  const auto build_result = UnicoreConfigProfileBuilder::Build(profile);
  if (build_result.status != UnicoreConfigProfileBuildStatus::kOk)
  {
    plan.status = ReceiverAutoConfigPlanStatus::kBuildError;
    plan.error_message = build_result.error_message;
    return plan;
  }

  plan.commands = build_result.commands;
  SummarizeCommands(plan);
  plan.validation.production_ready = true;
  plan.validation.ready_to_execute =
      request.apply_mode != ReceiverAutoConfigApplyMode::kDryRun;

  if (request.apply_mode == ReceiverAutoConfigApplyMode::kPersistent)
  {
    ApplyPersistentWarningsAndRollback(plan);
  }
  else
  {
    ApplyRuntimeRollback(plan);
  }

  return plan;
}

ReceiverAutoConfigPlan BuildNmeaUnsupportedPlan(const ReceiverAutoConfigRequest& request)
{
  ReceiverAutoConfigPlan plan = MakeBasePlan(request);
  plan.vendor = ReceiverVendor::kGeneric;
  plan.receiver_family_name = "NMEA";
  plan.capabilities_known = true;
  plan.capabilities = NmeaDriver{}.capabilities();
  plan.status = ReceiverAutoConfigPlanStatus::kUnsupportedReceiver;
  plan.validation.receiver_recognized = true;
  plan.validation.config_supported = false;
  plan.validation.profile_supported = false;
  plan.validation.apply_mode_supported = false;
  plan.error_message =
      "generic NMEA receivers do not support portable live configuration planning";
  plan.unsupported_reason = plan.error_message;
  plan.rollback_expectation.summary = "no receiver configuration changes are planned";
  return plan;
}

}  // namespace

ReceiverAutoConfigPlan BuildReceiverAutoConfigPlan(
    const ReceiverAutoConfigRequest& request)
{
  ReceiverAutoConfigPlan plan = MakeBasePlan(request);
  CopyDiscoveryContext(plan);

  ReceiverDetectedFamily effective_family = request.receiver_family;
  if (request.discovery_result.has_value())
  {
    const auto& discovery = *request.discovery_result;
    if (request.receiver_family != ReceiverDetectedFamily::kUnknown &&
        request.receiver_family != discovery.detected_family)
    {
      plan.status = ReceiverAutoConfigPlanStatus::kInvalidArgument;
      plan.error_message =
          "receiver family request does not match the supplied discovery result";
      return plan;
    }
    effective_family = discovery.detected_family;
  }

  plan.request.receiver_family = effective_family;

  if (effective_family == ReceiverDetectedFamily::kUnknown)
  {
    plan.status = ReceiverAutoConfigPlanStatus::kUnsupportedReceiver;
    plan.validation.receiver_recognized = false;
    plan.rollback_expectation.summary = "no receiver configuration changes are planned";
    plan.error_message = request.discovery_result.has_value()
                             ? (!request.discovery_result->reason.empty()
                                    ? request.discovery_result->reason
                                    : request.discovery_result->note)
                             : "receiver family is unknown";
    if (plan.error_message.empty())
    {
      plan.error_message = "receiver family is unknown";
    }
    plan.unsupported_reason = plan.error_message;
    return plan;
  }

  switch (effective_family)
  {
    case ReceiverDetectedFamily::kUblox:
      plan = BuildUbloxPlan(plan.request);
      break;
    case ReceiverDetectedFamily::kUnicore:
      plan = BuildUnicorePlan(plan.request);
      break;
    case ReceiverDetectedFamily::kNmea:
      plan = BuildNmeaUnsupportedPlan(plan.request);
      break;
    case ReceiverDetectedFamily::kUnknown:
      break;
  }

  CopyDiscoveryContext(plan);

  if (plan.status == ReceiverAutoConfigPlanStatus::kOk &&
      request.apply_mode == ReceiverAutoConfigApplyMode::kDryRun)
  {
    plan.warnings.push_back(
        "dry-run planning does not perform receiver writes");
    plan.validation.ready_to_execute = false;
  }

  if (plan.status != ReceiverAutoConfigPlanStatus::kOk &&
      plan.unsupported_reason.empty() &&
      !plan.error_message.empty() &&
      (plan.status == ReceiverAutoConfigPlanStatus::kUnsupportedReceiver ||
       plan.status == ReceiverAutoConfigPlanStatus::kUnsupportedProfile ||
       plan.status == ReceiverAutoConfigPlanStatus::kUnsupportedApplyMode))
  {
    plan.unsupported_reason = plan.error_message;
  }

  return plan;
}

ReceiverAutoConfigPlan BuildReceiverAutoConfigPlan(
    const ReceiverProbeResult& discovery_result,
    const ReceiverAutoConfigProfile requested_profile,
    const ReceiverAutoConfigApplyMode apply_mode,
    const std::optional<std::uint32_t> config_baud,
    const std::optional<double> rate_hz)
{
  ReceiverAutoConfigRequest request;
  request.receiver_family = discovery_result.detected_family;
  request.discovery_result = discovery_result;
  request.requested_profile = requested_profile;
  request.apply_mode = apply_mode;
  request.config_baud = config_baud;
  request.rate_hz = rate_hz;
  return BuildReceiverAutoConfigPlan(request);
}

const char* ToString(const ReceiverAutoConfigProfile profile)
{
  switch (profile)
  {
    case ReceiverAutoConfigProfile::kRover:
      return "rover";
    case ReceiverAutoConfigProfile::kBase:
      return "base";
    case ReceiverAutoConfigProfile::kDiagnostics:
      return "diagnostics";
  }

  return "rover";
}

const char* ToString(const ReceiverAutoConfigApplyMode apply_mode)
{
  switch (apply_mode)
  {
    case ReceiverAutoConfigApplyMode::kDryRun:
      return "dry_run";
    case ReceiverAutoConfigApplyMode::kRuntimeOnly:
      return "runtime_only";
    case ReceiverAutoConfigApplyMode::kPersistent:
      return "persistent";
  }

  return "dry_run";
}

const char* ToString(const ReceiverAutoConfigPlanStatus status)
{
  switch (status)
  {
    case ReceiverAutoConfigPlanStatus::kOk:
      return "ok";
    case ReceiverAutoConfigPlanStatus::kInvalidArgument:
      return "invalid_argument";
    case ReceiverAutoConfigPlanStatus::kUnsupportedReceiver:
      return "unsupported_receiver";
    case ReceiverAutoConfigPlanStatus::kUnsupportedProfile:
      return "unsupported_profile";
    case ReceiverAutoConfigPlanStatus::kUnsupportedApplyMode:
      return "unsupported_apply_mode";
    case ReceiverAutoConfigPlanStatus::kBuildError:
      return "build_error";
  }

  return "build_error";
}

}  // namespace universal_gnss_driver
