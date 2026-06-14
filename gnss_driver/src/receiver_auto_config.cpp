#include "universal_gnss_driver/receiver_auto_config.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
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
using universal_gnss_protocols::UbxCfgConstellation;

constexpr std::uint8_t kUnicoreRecommendedSignalGroupPrimary = 3u;
constexpr std::uint8_t kUnicoreRecommendedSignalGroupSecondary = 6u;

std::string ToLowerCopy(std::string_view text)
{
  std::string normalized(text);
  for (char& c : normalized)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return normalized;
}

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

void ApplyNoChangeRollback(ReceiverAutoConfigPlan& plan)
{
  plan.rollback_expectation.changes_are_temporary = true;
  plan.rollback_expectation.operator_action_required = false;
  plan.rollback_expectation.summary = "no receiver configuration changes are planned";
  plan.rollback_expectation.operator_action = "none";
}

void ApplyFactoryResetWarningsAndRollback(ReceiverAutoConfigPlan& plan)
{
  plan.validation.production_ready = false;
  plan.validation.ready_to_execute = false;
  plan.warnings.push_back(
      "factory reset clears saved receiver configuration and restarts the receiver");
  plan.warnings.push_back(
      "Unicore FRESET resets the active serial baud rate to 115200 bps");
  plan.warnings.push_back(
      "live factory-reset execution remains guarded until reconnect/probe handling is implemented");
  plan.rollback_expectation.changes_are_temporary = false;
  plan.rollback_expectation.operator_action_required = true;
  plan.rollback_expectation.summary =
      "factory reset clears saved receiver configuration and requires manual reprovisioning";
  plan.rollback_expectation.operator_action =
      "reconnect at 115200 bps, rediscover the receiver, and reapply a known-good profile";
}

void ApplyFactoryResetRecoveryWarningsAndRollback(ReceiverAutoConfigPlan& plan,
                                                  const std::uint32_t recovery_baud)
{
  plan.validation.production_ready = true;
  plan.validation.ready_to_execute =
      plan.request.apply_mode != ReceiverAutoConfigApplyMode::kDryRun;
  plan.warnings.push_back(
      "factory reset clears saved receiver configuration and restarts the receiver");
  plan.warnings.push_back(
      "Unicore FRESET resets the active serial baud rate to 115200 bps");
  plan.warnings.push_back(
      "live apply must reconnect/probe at 115200 bps with an active VERSIONA query, reconfigure COM1, then continue at " +
      std::to_string(recovery_baud) + " bps");
  plan.warnings.push_back(
      "after FRESET the receiver may need about 30 seconds or slightly more before it starts responding again");

  if (plan.request.apply_mode == ReceiverAutoConfigApplyMode::kPersistent)
  {
    plan.warnings.push_back(
        "persistent Unicore profile apply performs FRESET first so the saved profile is rebuilt from a clean baseline");
    plan.warnings.push_back(
        "persistent recovery will finish with SAVECONFIG after the post-reset profile is restored");
    plan.rollback_expectation.summary =
        "factory reset clears saved receiver configuration before restoring a saved Unicore profile";
    plan.rollback_expectation.operator_action =
        "reconnect at " + std::to_string(recovery_baud) +
        " bps and reapply a different saved profile if rollback is needed";
  }
  else
  {
    plan.warnings.push_back(
        "runtime-only recovery re-enables a known-good rover profile after the reset but does not save it; the receiver will keep factory defaults after the next reboot");
    plan.rollback_expectation.summary =
        "factory reset permanently restores saved defaults before a temporary rover profile is re-applied";
    plan.rollback_expectation.operator_action =
        "reconnect at " + std::to_string(recovery_baud) +
        " bps and rerun a persistent profile if factory defaults should not remain saved";
  }

  plan.rollback_expectation.changes_are_temporary = false;
  plan.rollback_expectation.operator_action_required = true;
}

bool ProfileLeavesReceiverUnchanged(const ReceiverAutoConfigProfile profile)
{
  return profile == ReceiverAutoConfigProfile::kRuntimeOnly;
}

bool ProfileSupportsSignalProfileOverride(const ReceiverAutoConfigProfile profile)
{
  return profile == ReceiverAutoConfigProfile::kRoverHighPrecision ||
         profile == ReceiverAutoConfigProfile::kRoverHighPrecisionDebug ||
         profile == ReceiverAutoConfigProfile::kFactoryReset;
}

bool ProfileSupportsRateOverride(const ReceiverAutoConfigProfile profile)
{
  return profile == ReceiverAutoConfigProfile::kRoverHighPrecision ||
         profile == ReceiverAutoConfigProfile::kRoverHighPrecisionDebug ||
         profile == ReceiverAutoConfigProfile::kFactoryReset;
}

std::uint32_t ResolveUnicoreRecoveryBaud(const ReceiverAutoConfigRequest& request)
{
  if (request.config_baud.has_value())
  {
    return *request.config_baud;
  }

  return 921600u;
}

ReceiverAutoConfigPlan MakeUnsupportedProfilePlan(
    const ReceiverAutoConfigRequest& request,
    const ReceiverVendor vendor,
    std::string family_name,
    const ReceiverCapabilities& capabilities,
    const std::string& message)
{
  ReceiverAutoConfigPlan plan = MakeBasePlan(request);
  plan.vendor = vendor;
  plan.receiver_family_name = std::move(family_name);
  plan.capabilities_known = true;
  plan.capabilities = capabilities;
  plan.status = ReceiverAutoConfigPlanStatus::kUnsupportedProfile;
  plan.validation.receiver_recognized = true;
  plan.validation.config_supported = true;
  plan.validation.profile_supported = false;
  plan.validation.apply_mode_supported = true;
  plan.error_message = message;
  plan.unsupported_reason = message;
  ApplyNoChangeRollback(plan);
  return plan;
}

void AppendRuntimeOnlySignalProfileWarning(ReceiverAutoConfigPlan& plan)
{
  if (!plan.request.signal_profile.has_value())
  {
    return;
  }

  plan.warnings.push_back(
      "signal_profile=" + std::string(ToString(*plan.request.signal_profile)) +
      " is unsupported with the runtime_only profile because runtime_only does not send receiver commands");
}

ReceiverAutoConfigPlan MakeNoChangePlan(
    const ReceiverAutoConfigRequest& request,
    const ReceiverVendor vendor,
    std::string family_name,
    const ReceiverCapabilities& capabilities)
{
  ReceiverAutoConfigPlan plan = MakeBasePlan(request);
  plan.vendor = vendor;
  plan.receiver_family_name = std::move(family_name);
  plan.capabilities_known = true;
  plan.capabilities = capabilities;
  plan.validation.receiver_recognized = true;
  plan.validation.config_supported = true;
  plan.validation.profile_supported = true;

  if (request.apply_mode == ReceiverAutoConfigApplyMode::kPersistent)
  {
    plan.status = ReceiverAutoConfigPlanStatus::kUnsupportedApplyMode;
    plan.validation.apply_mode_supported = false;
    plan.error_message =
        "runtime_only profile does not support persistent apply because it does not modify receiver configuration";
    plan.unsupported_reason = plan.error_message;
    ApplyNoChangeRollback(plan);
    return plan;
  }

  plan.validation.apply_mode_supported = true;

  if (request.config_baud.has_value() || request.rate_hz.has_value())
  {
    plan.status = ReceiverAutoConfigPlanStatus::kInvalidArgument;
    plan.error_message =
        "runtime_only profile does not accept configuration overrides because it does not send receiver commands";
    ApplyNoChangeRollback(plan);
    return plan;
  }

  plan.validation.production_ready = true;
  plan.validation.ready_to_execute =
      request.apply_mode != ReceiverAutoConfigApplyMode::kDryRun;
  AppendRuntimeOnlySignalProfileWarning(plan);
  plan.warnings.push_back(
      "runtime_only profile leaves the receiver configuration unchanged");
  ApplyNoChangeRollback(plan);
  return plan;
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

void ApplyUbloxSignalProfile(const ReceiverAutoConfigRequest& request,
                             ReceiverAutoConfigPlan& plan,
                             UbloxConfigProfile& profile);

void ApplyUnicoreSignalProfile(const ReceiverAutoConfigRequest& request,
                               ReceiverAutoConfigPlan& plan,
                               UnicoreConfigProfile& profile);

ReceiverAutoConfigPlan BuildUbloxPlan(const ReceiverAutoConfigRequest& request)
{
  ReceiverAutoConfigPlan plan = MakeBasePlan(request);
  plan.vendor = ReceiverVendor::kUblox;
  plan.receiver_family_name = "F9/F10";
  plan.capabilities_known = true;
  plan.capabilities = UbloxDriver{}.capabilities();
  plan.validation.receiver_recognized = true;
  plan.validation.config_supported = true;

  if (ProfileLeavesReceiverUnchanged(request.requested_profile))
  {
    return MakeNoChangePlan(request,
                            ReceiverVendor::kUblox,
                            "F9/F10",
                            UbloxDriver{}.capabilities());
  }

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
    case ReceiverAutoConfigProfile::kRoverHighPrecision:
      plan.validation.apply_mode_supported = true;
      plan.validation.profile_supported = true;
      profile = UbloxConfigProfileBuilder::BuildUbloxRoverProfile(safety_level, layers);
      break;
    case ReceiverAutoConfigProfile::kRoverHighPrecisionDebug:
      plan.validation.apply_mode_supported = true;
      plan.validation.profile_supported = true;
      profile = UbloxConfigProfileBuilder::BuildUbloxDiagnosticsProfile(safety_level, layers);
      break;
    case ReceiverAutoConfigProfile::kFactoryReset:
      return MakeUnsupportedProfilePlan(
          request,
          ReceiverVendor::kUblox,
          "F9/F10",
          UbloxDriver{}.capabilities(),
          "u-blox factory_reset profile is not yet implemented by the portable config layer");
    case ReceiverAutoConfigProfile::kRuntimeOnly:
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
  ApplyUbloxSignalProfile(request, plan, profile);

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

bool IsRateControlledUnicoreMessage(const UnicoreOutputMessageKind message)
{
  return message == UnicoreOutputMessageKind::kBestnava;
}

void ApplyUnicoreRecommendedSignalGroup(UnicoreConfigProfile& profile)
{
  profile.signal_config = UnicoreSignalConfig{
      {kUnicoreRecommendedSignalGroupPrimary, kUnicoreRecommendedSignalGroupSecondary}};
}

void ApplyUnicoreMinimalOutputLoad(UnicoreConfigProfile& profile)
{
  profile.output_messages.erase(
      std::remove_if(
          profile.output_messages.begin(),
          profile.output_messages.end(),
          [](const UnicoreOutputMessageRate& output) {
            return output.message == UnicoreOutputMessageKind::kGpgsv ||
                   output.message == UnicoreOutputMessageKind::kGpgst ||
                   output.message == UnicoreOutputMessageKind::kPvtslna;
          }),
      profile.output_messages.end());
}

void ApplyUnicoreSignalProfile(const ReceiverAutoConfigRequest& request,
                               ReceiverAutoConfigPlan& plan,
                               UnicoreConfigProfile& profile)
{
  if (!request.signal_profile.has_value() ||
      !ProfileSupportsSignalProfileOverride(request.requested_profile))
  {
    return;
  }

  switch (*request.signal_profile)
  {
    case ReceiverAutoConfigSignalProfile::kBalanced:
    case ReceiverAutoConfigSignalProfile::kHighPrecision:
    case ReceiverAutoConfigSignalProfile::kAllSignals:
      ApplyUnicoreRecommendedSignalGroup(profile);
      return;
    case ReceiverAutoConfigSignalProfile::kMinimal:
      ApplyUnicoreRecommendedSignalGroup(profile);
      ApplyUnicoreMinimalOutputLoad(profile);
      plan.warnings.push_back(
          "signal_profile=minimal keeps the validated CONFIG SIGNALGROUP 3 6 mapping and reduces auxiliary Unicore output messages to lower serial link load");
      return;
    case ReceiverAutoConfigSignalProfile::kCustom:
      plan.warnings.push_back(
          "custom signal_profile is reserved for vendor-specific advanced settings; the portable Unicore planner kept the default validated runtime mapping");
      return;
  }
}

void ApplyUbloxStandardConstellations(UbloxConfigProfile& profile)
{
  profile.constellations = {
      {UbxCfgConstellation::kGps, true},
      {UbxCfgConstellation::kGalileo, true},
      {UbxCfgConstellation::kBeiDou, true},
      {UbxCfgConstellation::kGlonass, true},
  };
}

void ApplyUbloxSignalProfile(const ReceiverAutoConfigRequest& request,
                             ReceiverAutoConfigPlan& plan,
                             UbloxConfigProfile& profile)
{
  if (!request.signal_profile.has_value() ||
      !ProfileSupportsSignalProfileOverride(request.requested_profile))
  {
    return;
  }

  switch (*request.signal_profile)
  {
    case ReceiverAutoConfigSignalProfile::kBalanced:
    case ReceiverAutoConfigSignalProfile::kHighPrecision:
      ApplyUbloxStandardConstellations(profile);
      return;
    case ReceiverAutoConfigSignalProfile::kAllSignals:
      plan.warnings.push_back(
          "signal_profile=all_signals is not yet mapped to documented portable u-blox per-signal configuration; keeping the standard GPS/Galileo/BeiDou/GLONASS plan");
      return;
    case ReceiverAutoConfigSignalProfile::kMinimal:
      plan.warnings.push_back(
          "signal_profile=minimal is not yet mapped to a documented portable u-blox reduced-signal plan; keeping the standard GPS/Galileo/BeiDou/GLONASS configuration");
      return;
    case ReceiverAutoConfigSignalProfile::kCustom:
      plan.warnings.push_back(
          "custom signal_profile is not yet supported by the portable u-blox planner");
      return;
  }
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

  if (ProfileLeavesReceiverUnchanged(request.requested_profile))
  {
    return MakeNoChangePlan(request,
                            ReceiverVendor::kUnicore,
                            "UM98x",
                            UnicoreDriver{}.capabilities());
  }

  if (!ValidateRateHz(request, plan))
  {
    return plan;
  }

  if (!ValidateConfigBaud(request, plan))
  {
    return plan;
  }

  const bool requires_clean_reset_workflow =
      request.requested_profile == ReceiverAutoConfigProfile::kFactoryReset ||
      request.apply_mode == ReceiverAutoConfigApplyMode::kPersistent;

  if (request.config_baud.has_value() && !requires_clean_reset_workflow)
  {
    plan.status = ReceiverAutoConfigPlanStatus::kInvalidArgument;
    plan.error_message =
        "Unicore auto-configuration planning only supports baud overrides through the clean reset/recovery workflow";
    return plan;
  }

  plan.validation.profile_supported = true;
  plan.validation.apply_mode_supported = true;

  const auto persistence = request.apply_mode == ReceiverAutoConfigApplyMode::kPersistent
                               ? UnicorePersistenceTarget::kSaveConfig
                               : UnicorePersistenceTarget::kRuntimeOnly;

  UnicoreConfigProfile profile;
  switch (request.requested_profile)
  {
    case ReceiverAutoConfigProfile::kRoverHighPrecision:
      profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(persistence);
      break;
    case ReceiverAutoConfigProfile::kRoverHighPrecisionDebug:
      profile = UnicoreConfigProfileBuilder::BuildUnicoreDiagnosticsProfile(persistence);
      break;
    case ReceiverAutoConfigProfile::kFactoryReset:
      profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(persistence);
      break;
    case ReceiverAutoConfigProfile::kRuntimeOnly:
      break;
  }

  ApplyUnicoreSignalProfile(request, plan, profile);

  if (requires_clean_reset_workflow)
  {
    const auto recovery_baud = ResolveUnicoreRecoveryBaud(request);
    profile.com1_baud_rate = recovery_baud;

    if (request.rate_hz.has_value() &&
        ProfileSupportsRateOverride(request.requested_profile))
    {
      const double period_s = 1.0 / *request.rate_hz;
      for (auto& output : profile.output_messages)
      {
        if (IsRateControlledUnicoreMessage(output.message))
        {
          output.period_s = period_s;
        }
      }
    }

    const auto reset_result = UnicoreConfigProfileBuilder::Build(
        UnicoreConfigProfileBuilder::BuildUnicoreFactoryResetProfile());
    if (reset_result.status != UnicoreConfigProfileBuildStatus::kOk)
    {
      plan.status = ReceiverAutoConfigPlanStatus::kBuildError;
      plan.error_message = reset_result.error_message;
      return plan;
    }

    const auto recovery_result = UnicoreConfigProfileBuilder::Build(profile);
    if (recovery_result.status != UnicoreConfigProfileBuildStatus::kOk)
    {
      plan.status = ReceiverAutoConfigPlanStatus::kBuildError;
      plan.error_message = recovery_result.error_message;
      return plan;
    }

    plan.commands = reset_result.commands;
    plan.commands.insert(
        plan.commands.end(),
        recovery_result.commands.begin(),
        recovery_result.commands.end());
    SummarizeCommands(plan);
    ApplyFactoryResetRecoveryWarningsAndRollback(plan, recovery_baud);
    return plan;
  }

  if (request.rate_hz.has_value() &&
      ProfileSupportsRateOverride(request.requested_profile))
  {
    const double period_s = 1.0 / *request.rate_hz;
    for (auto& output : profile.output_messages)
    {
      if (IsRateControlledUnicoreMessage(output.message))
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
  if (request.requested_profile == ReceiverAutoConfigProfile::kFactoryReset)
  {
    ApplyFactoryResetWarningsAndRollback(plan);
    return plan;
  }

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

ReceiverAutoConfigPlan BuildNmeaPlan(const ReceiverAutoConfigRequest& request)
{
  if (ProfileLeavesReceiverUnchanged(request.requested_profile))
  {
    return MakeNoChangePlan(
        request, ReceiverVendor::kGeneric, "NMEA", NmeaDriver{}.capabilities());
  }

  ReceiverAutoConfigPlan plan = MakeUnsupportedProfilePlan(
      request,
      ReceiverVendor::kGeneric,
      "NMEA",
      NmeaDriver{}.capabilities(),
      "generic NMEA receivers only support the runtime_only profile because portable write-side configuration is not standardized");
  plan.validation.config_supported = false;
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
      plan = BuildNmeaPlan(plan.request);
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
    case ReceiverAutoConfigProfile::kRuntimeOnly:
      return "runtime_only";
    case ReceiverAutoConfigProfile::kRoverHighPrecision:
      return "rover_high_precision";
    case ReceiverAutoConfigProfile::kRoverHighPrecisionDebug:
      return "rover_high_precision_debug";
    case ReceiverAutoConfigProfile::kFactoryReset:
      return "factory_reset";
  }

  return "runtime_only";
}

std::optional<ReceiverAutoConfigProfile> ParseReceiverAutoConfigProfile(
    const std::string_view profile)
{
  const std::string normalized = ToLowerCopy(profile);
  if (normalized == "runtime_only" || normalized == "runtime-only")
  {
    return ReceiverAutoConfigProfile::kRuntimeOnly;
  }
  if (normalized == "rover_high_precision" || normalized == "rover-high-precision" ||
      normalized == "rover")
  {
    return ReceiverAutoConfigProfile::kRoverHighPrecision;
  }
  if (normalized == "rover_high_precision_debug" ||
      normalized == "rover-high-precision-debug" ||
      normalized == "diagnostics")
  {
    return ReceiverAutoConfigProfile::kRoverHighPrecisionDebug;
  }
  if (normalized == "factory_reset" || normalized == "factory-reset")
  {
    return ReceiverAutoConfigProfile::kFactoryReset;
  }
  return std::nullopt;
}

std::optional<ReceiverAutoConfigSignalProfile> ParseReceiverAutoConfigSignalProfile(
    const std::string_view signal_profile)
{
  const std::string normalized = ToLowerCopy(signal_profile);
  if (normalized == "balanced")
  {
    return ReceiverAutoConfigSignalProfile::kBalanced;
  }
  if (normalized == "high_precision" || normalized == "high-precision")
  {
    return ReceiverAutoConfigSignalProfile::kHighPrecision;
  }
  if (normalized == "all_signals" || normalized == "all-signals")
  {
    return ReceiverAutoConfigSignalProfile::kAllSignals;
  }
  if (normalized == "minimal" || normalized == "low_bandwidth" ||
      normalized == "low-bandwidth")
  {
    return ReceiverAutoConfigSignalProfile::kMinimal;
  }
  if (normalized == "custom")
  {
    return ReceiverAutoConfigSignalProfile::kCustom;
  }
  return std::nullopt;
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

const char* ToString(const ReceiverAutoConfigSignalProfile signal_profile)
{
  switch (signal_profile)
  {
    case ReceiverAutoConfigSignalProfile::kBalanced:
      return "balanced";
    case ReceiverAutoConfigSignalProfile::kHighPrecision:
      return "high_precision";
    case ReceiverAutoConfigSignalProfile::kAllSignals:
      return "all_signals";
    case ReceiverAutoConfigSignalProfile::kMinimal:
      return "minimal";
    case ReceiverAutoConfigSignalProfile::kCustom:
      return "custom";
  }

  return "balanced";
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
