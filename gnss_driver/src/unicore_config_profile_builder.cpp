#include "universal_gnss_driver/unicore_config_profile_builder.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace universal_gnss_driver
{

namespace
{

constexpr ReceiverTargetSelector kUnicoreTarget{
    ReceiverVendor::kUnicore,
    "UM98x",
    "placeholder",
    "unicore_um98x_placeholder",
};

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

ReceiverCommand MakeTextCommand(const ReceiverCommandKind kind,
                                const ReceiverCommandSafetyLevel safety_level,
                                const std::string& text_command)
{
  ReceiverCommand command;
  command.kind = kind;
  command.target = kUnicoreTarget;
  command.expected_response = ReceiverResponseKind::kTextPayload;
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
  if (!SupportsRuntimeMode(profile.mode))
  {
    result.status = UnicoreConfigProfileBuildStatus::kInvalidArgument;
    result.error_message =
        "base and survey orchestration are deferred from the portable Unicore config builder";
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
                   const ReceiverCommandKind kind,
                   const ReceiverCommandSafetyLevel safety_level,
                   const std::string& text_command)
{
  if (!text_command.empty())
  {
    commands.push_back(MakeTextCommand(kind, safety_level, text_command));
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

  AppendCommand(result.commands,
                ReceiverCommandKind::kApplyConfigProfile,
                ReceiverCommandSafetyLevel::kRuntime,
                BuildModeCommand(profile.mode));

  if (profile.nmea_version.has_value())
  {
    AppendCommand(result.commands,
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kRuntime,
                  std::string("CONFIG NMEA0183 ") +
                      ToNmeaVersionString(*profile.nmea_version));
  }

  if (profile.rtk_timeout_s.has_value())
  {
    AppendCommand(result.commands,
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kRuntime,
                  "CONFIG RTK TIMEOUT " + std::to_string(*profile.rtk_timeout_s));
  }

  if (profile.rtk_reliability.has_value())
  {
    AppendCommand(
        result.commands,
        ReceiverCommandKind::kApplyConfigProfile,
        ReceiverCommandSafetyLevel::kRuntime,
        "CONFIG RTK RELIABILITY " + std::to_string(profile.rtk_reliability->primary) +
            " " + std::to_string(profile.rtk_reliability->secondary));
  }

  if (profile.dgps_timeout_s.has_value())
  {
    AppendCommand(result.commands,
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kRuntime,
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
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kPersistent,
                  command);
  }

  for (const auto& output : profile.output_messages)
  {
    AppendCommand(result.commands,
                  ReceiverCommandKind::kSetProtocolOutputs,
                  ReceiverCommandSafetyLevel::kRuntime,
                  BuildOutputCommand(output));
  }

  if (profile.persistence == UnicorePersistenceTarget::kSaveConfig)
  {
    AppendCommand(result.commands,
                  ReceiverCommandKind::kApplyConfigProfile,
                  ReceiverCommandSafetyLevel::kPersistent,
                  "SAVECONFIG");
  }

  return result;
}

UnicoreConfigProfile UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(
    const UnicorePersistenceTarget persistence)
{
  UnicoreConfigProfile profile;
  profile.config_kind = ReceiverConfigProfileKind::kRover;
  profile.mode = UnicoreMode::kRover;
  profile.nmea_version = UnicoreNmeaVersion::kV411;
  profile.rtk_timeout_s = 10u;
  profile.rtk_reliability = UnicoreRtkReliability{3, 1};
  profile.dgps_timeout_s = 600u;
  profile.output_messages = {
      {UnicoreOutputMessageKind::kGpgga, 0.2},
      {UnicoreOutputMessageKind::kPvtslna, 0.2},
      {UnicoreOutputMessageKind::kBestnava, 0.2},
      {UnicoreOutputMessageKind::kRtkstatusa, 1.0},
      {UnicoreOutputMessageKind::kRtcmstatusa, std::nullopt},
  };
  profile.persistence = persistence;
  return profile;
}

UnicoreConfigProfile UnicoreConfigProfileBuilder::BuildUnicoreDiagnosticsProfile(
    const UnicorePersistenceTarget persistence)
{
  UnicoreConfigProfile profile = BuildUnicoreRoverProfile(persistence);
  profile.config_kind = ReceiverConfigProfileKind::kDiagnosticsOutput;
  profile.output_messages.push_back(
      UnicoreOutputMessageRate{UnicoreOutputMessageKind::kSatsinfoa, 1.0});
  return profile;
}

}  // namespace universal_gnss_driver
