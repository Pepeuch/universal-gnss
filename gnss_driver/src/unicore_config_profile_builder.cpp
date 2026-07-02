#include "universal_gnss_driver/unicore_config_profile_builder.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace universal_gnss_driver
{

namespace
{

constexpr const char* kCrLf = "\r\n";

std::string FormatPeriodSeconds(const double period_s)
{
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(3) << period_s;
  std::string text = stream.str();

  while (!text.empty() && text.back() == '0')
  {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.')
  {
    text.pop_back();
  }
  if (text.empty())
  {
    return "0";
  }
  return text;
}

std::string FormatTextCommand(const std::string& command)
{
  return command + kCrLf;
}

std::string BuildCom1Command(const std::uint32_t baud_rate)
{
  return "CONFIG COM1 " + std::to_string(baud_rate) + " 8 n 1";
}

ReceiverCommand MakeTextCommand(const ReceiverCommandKind kind,
                                const ReceiverTargetSelector& target,
                                const ReceiverCommandSafetyLevel safety_level,
                                const ReceiverResponseKind expected_response,
                                const std::string& text_command)
{
  ReceiverCommand command;
  command.kind = kind;
  command.target = target;
  command.expected_response = expected_response;
  command.safety_level = safety_level;
  SetTextPayload(command, FormatTextCommand(text_command));
  return command;
}

const char* ToNmeaVersionString(const UnicoreNmeaVersion version)
{
  switch (version)
  {
    case UnicoreNmeaVersion::kV410:
      return "V410";
    case UnicoreNmeaVersion::kV411:
      return "V411";
  }

  return "";
}

bool SupportsRuntimeMode(const UnicoreMode mode)
{
  return mode == UnicoreMode::kUnspecified || mode == UnicoreMode::kRover;
}

std::string BuildModeCommand(const UnicoreMode mode)
{
  switch (mode)
  {
    case UnicoreMode::kUnspecified:
      return {};
    case UnicoreMode::kRover:
      return "MODE ROVER";
    case UnicoreMode::kBase:
      return "MODE BASE";
    case UnicoreMode::kSurvey:
      return "MODE ROVER SURVEY";
  }

  return {};
}

const char* ToOutputMessageName(const UnicoreOutputMessageKind message)
{
  switch (message)
  {
    case UnicoreOutputMessageKind::kGpgga:
      return "GPGGA";
    case UnicoreOutputMessageKind::kGpgsv:
      return "GPGSV";
    case UnicoreOutputMessageKind::kGpgst:
      return "GPGST";
    case UnicoreOutputMessageKind::kPvtslna:
      return "PVTSLNA";
    case UnicoreOutputMessageKind::kBestnava:
      return "BESTNAVA";
    case UnicoreOutputMessageKind::kRtkstatusa:
      return "RTKSTATUSA";
    case UnicoreOutputMessageKind::kRtcmstatusa:
      return "RTCMSTATUSA";
    case UnicoreOutputMessageKind::kSatsinfoa:
      return "SATSINFOA";
  }

  return "";
}

bool UsesLogOntimeSyntax(const UnicoreOutputMessageKind message)
{
  return message == UnicoreOutputMessageKind::kGpgga ||
         message == UnicoreOutputMessageKind::kPvtslna;
}

bool UsesOnChangedSyntax(const UnicoreOutputMessageKind message)
{
  return message == UnicoreOutputMessageKind::kRtcmstatusa;
}

std::string BuildOutputCommand(const UnicoreOutputMessageRate& output)
{
  const std::string message = ToOutputMessageName(output.message);
  if (UsesOnChangedSyntax(output.message))
  {
    return message + " ONCHANGED";
  }

  const std::string period_text = FormatPeriodSeconds(*output.period_s);
  if (UsesLogOntimeSyntax(output.message))
  {
    return "LOG " + message + " ONTIME " + period_text;
  }

  return message + " " + period_text;
}

bool ValidateOutputRate(UnicoreConfigProfileBuildResult& result,
                        const UnicoreOutputMessageRate& output)
{
  if (UsesOnChangedSyntax(output.message))
  {
    return true;
  }

  if (!output.period_s.has_value() || *output.period_s <= 0.0)
  {
    result.status = UnicoreConfigProfileBuildStatus::kInvalidArgument;
    result.error_message =
        "unicore output messages using periodic syntax require a positive period";
    return false;
  }

  return true;
}

bool ValidateProfile(UnicoreConfigProfileBuildResult& result,
                     const UnicoreConfigProfile& profile)
{
  if (profile.factory_reset)
  {
    if (profile.mode != UnicoreMode::kUnspecified ||
        profile.com1_baud_rate.has_value() ||
        profile.nmea_version.has_value() ||
        profile.rtk_timeout_s.has_value() ||
        profile.dgps_timeout_s.has_value() ||
        profile.rtk_reliability.has_value() ||
        profile.signal_config.has_value() ||
        profile.clear_current_port_outputs ||
        !profile.output_messages.empty() ||
        profile.persistence != UnicorePersistenceTarget::kRuntimeOnly)
    {
      result.status = UnicoreConfigProfileBuildStatus::kInvalidArgument;
      result.error_message =
          "unicore factory-reset profile cannot be combined with other portable config mutations";
      return false;
    }

    return true;
  }

  if (!SupportsRuntimeMode(profile.mode))
  {
    result.status = UnicoreConfigProfileBuildStatus::kInvalidArgument;
    result.error_message =
        "base and survey orchestration are deferred from the portable Unicore config builder";
    return false;
  }

  if (profile.com1_baud_rate.has_value() && *profile.com1_baud_rate == 0u)
  {
    result.status = UnicoreConfigProfileBuildStatus::kInvalidArgument;
    result.error_message = "unicore COM1 baud rate must be non-zero";
    return false;
  }

  if (profile.rtk_timeout_s.has_value() && *profile.rtk_timeout_s == 0u)
  {
    result.status = UnicoreConfigProfileBuildStatus::kInvalidArgument;
    result.error_message = "unicore RTK timeout must be non-zero";
    return false;
  }

  if (profile.dgps_timeout_s.has_value() && *profile.dgps_timeout_s == 0u)
  {
    result.status = UnicoreConfigProfileBuildStatus::kInvalidArgument;
    result.error_message = "unicore DGPS timeout must be non-zero";
    return false;
  }

  if (profile.signal_config.has_value() && profile.signal_config->groups.empty())
  {
    result.status = UnicoreConfigProfileBuildStatus::kInvalidArgument;
    result.error_message = "unicore signal-group configuration requires at least one group id";
    return false;
  }

  for (const auto& output : profile.output_messages)
  {
    if (!ValidateOutputRate(result, output))
    {
      return false;
    }
  }

  return true;
}

void AppendCommand(std::vector<ReceiverCommand>& commands,
                   const ReceiverTargetSelector& target,
                   const ReceiverCommandKind kind,
                   const ReceiverCommandSafetyLevel safety_level,
                   const ReceiverResponseKind expected_response,
                   const std::string& text_command)
{
  if (!text_command.empty())
  {
    commands.push_back(MakeTextCommand(kind, target, safety_level, expected_response, text_command));
  }
}

void SetOutputPeriod(UnicoreConfigProfile& profile,
                     const UnicoreOutputMessageKind message,
                     const std::optional<double> period_s)
{
  for (auto& output : profile.output_messages)
  {
    if (output.message == message)
    {
      output.period_s = period_s;
      return;
    }
  }
}

}  // namespace

UnicoreConfigProfileBuildResult UnicoreConfigProfileBuilder::Build(
    const UnicoreConfigProfile& profile)
{
  UnicoreConfigProfileBuildResult result;
  if (!ValidateProfile(result, profile))
  {
    return result;
  }

  const ReceiverTargetSelector target =
      profile.target.vendor == ReceiverVendor::kUnicore
          ? profile.target
          : BuildUnicoreTargetSelector(ResolveUnicoreModelProfile());

  if (profile.factory_reset)
  {
    AppendCommand(result.commands,
                  target,
                  ReceiverCommandKind::kReset,
                  ReceiverCommandSafetyLevel::kFactoryReset,
                  ReceiverResponseKind::kNone,
                  "FRESET");
    return result;
  }

  if (profile.com1_baud_rate.has_value())
  {
    AppendCommand(result.commands,
                  target,
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kRuntime,
                  ReceiverResponseKind::kNone,
                  BuildCom1Command(*profile.com1_baud_rate));
  }

  AppendCommand(result.commands,
                target,
                ReceiverCommandKind::kApplyConfigProfile,
                ReceiverCommandSafetyLevel::kRuntime,
                ReceiverResponseKind::kTextPayload,
                BuildModeCommand(profile.mode));

  if (profile.nmea_version.has_value())
  {
    AppendCommand(result.commands,
                  target,
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kRuntime,
                  ReceiverResponseKind::kTextPayload,
                  std::string("CONFIG NMEA0183 ") +
                      ToNmeaVersionString(*profile.nmea_version));
  }

  if (profile.rtk_timeout_s.has_value())
  {
    AppendCommand(result.commands,
                  target,
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kRuntime,
                  ReceiverResponseKind::kTextPayload,
                  "CONFIG RTK TIMEOUT " + std::to_string(*profile.rtk_timeout_s));
  }

  if (profile.rtk_reliability.has_value())
  {
    AppendCommand(
        result.commands,
        target,
        ReceiverCommandKind::kApplyConfigProfile,
        ReceiverCommandSafetyLevel::kRuntime,
        ReceiverResponseKind::kTextPayload,
        "CONFIG RTK RELIABILITY " + std::to_string(profile.rtk_reliability->primary) +
            " " + std::to_string(profile.rtk_reliability->secondary));
  }

  if (profile.dgps_timeout_s.has_value())
  {
    AppendCommand(result.commands,
                  target,
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kRuntime,
                  ReceiverResponseKind::kTextPayload,
                  "CONFIG DGPS TIMEOUT " + std::to_string(*profile.dgps_timeout_s));
  }

  if (profile.signal_config.has_value())
  {
    std::string command = "CONFIG SIGNALGROUP";
    for (const auto group : profile.signal_config->groups)
    {
      command += " " + std::to_string(group);
    }
    AppendCommand(result.commands,
                  target,
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kRuntime,
                  ReceiverResponseKind::kTextPayload,
                  command);
  }

  if (profile.clear_current_port_outputs)
  {
    AppendCommand(result.commands,
                  target,
                  ReceiverCommandKind::kSetProtocolOutputs,
                  ReceiverCommandSafetyLevel::kRuntime,
                  ReceiverResponseKind::kTextPayload,
                  "UNLOG");
  }

  for (const auto& output : profile.output_messages)
  {
    AppendCommand(result.commands,
                  target,
                  ReceiverCommandKind::kSetProtocolOutputs,
                  ReceiverCommandSafetyLevel::kRuntime,
                  ReceiverResponseKind::kTextPayload,
                  BuildOutputCommand(output));
  }

  if (profile.persistence == UnicorePersistenceTarget::kSaveConfig)
  {
    AppendCommand(result.commands,
                  target,
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kPersistent,
                  ReceiverResponseKind::kTextPayload,
                  "SAVECONFIG");
  }

  return result;
}

UnicoreConfigProfile UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(
    const UnicorePersistenceTarget persistence)
{
  return BuildUnicoreRoverProfile(ResolveUnicoreModelProfile(), persistence);
}

UnicoreConfigProfile UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(
    const UnicoreModelProfile& model_profile,
    const UnicorePersistenceTarget persistence)
{
  UnicoreConfigProfile profile;
  profile.target = BuildUnicoreTargetSelector(model_profile);
  profile.config_kind = ReceiverConfigProfileKind::kRover;
  profile.mode = UnicoreMode::kRover;
  profile.nmea_version = UnicoreNmeaVersion::kV411;
  profile.rtk_timeout_s = 10u;
  profile.rtk_reliability = UnicoreRtkReliability{3, 1};
  profile.dgps_timeout_s = 600u;
  if (const auto* signal_group = FindUnicorePortableRoverSignalGroupSelection(model_profile);
      signal_group != nullptr)
  {
    profile.signal_config = UnicoreSignalConfig{signal_group->groups};
  }
  profile.clear_current_port_outputs = true;
  profile.output_messages = {
      {UnicoreOutputMessageKind::kGpgga, 1.0},
      {UnicoreOutputMessageKind::kGpgsv, 1.0},
      {UnicoreOutputMessageKind::kGpgst, 1.0},
      {UnicoreOutputMessageKind::kPvtslna, 1.0},
      {UnicoreOutputMessageKind::kBestnava, 0.2},
      {UnicoreOutputMessageKind::kRtkstatusa, 1.0},
      {UnicoreOutputMessageKind::kRtcmstatusa, std::nullopt},
      {UnicoreOutputMessageKind::kSatsinfoa, 1.0},
  };
  profile.persistence = persistence;
  return profile;
}

UnicoreConfigProfile UnicoreConfigProfileBuilder::BuildUnicoreDiagnosticsProfile(
    const UnicorePersistenceTarget persistence)
{
  return BuildUnicoreDiagnosticsProfile(ResolveUnicoreModelProfile(), persistence);
}

UnicoreConfigProfile UnicoreConfigProfileBuilder::BuildUnicoreDiagnosticsProfile(
    const UnicoreModelProfile& model_profile,
    const UnicorePersistenceTarget persistence)
{
  UnicoreConfigProfile profile = BuildUnicoreRoverProfile(model_profile, persistence);
  profile.config_kind = ReceiverConfigProfileKind::kDiagnosticsOutput;
  SetOutputPeriod(profile, UnicoreOutputMessageKind::kPvtslna, 0.2);
  return profile;
}

UnicoreConfigProfile UnicoreConfigProfileBuilder::BuildUnicoreFactoryResetProfile()
{
  UnicoreConfigProfile profile;
  profile.target = BuildUnicoreTargetSelector(ResolveUnicoreModelProfile());
  profile.factory_reset = true;
  return profile;
}

}  // namespace universal_gnss_driver
