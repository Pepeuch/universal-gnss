#include "universal_gnss_tools/config_plan.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include "universal_gnss_tools/profile_preview.hpp"

namespace universal_gnss_tools
{

namespace
{

using universal_gnss_driver::BuildReceiverAutoConfigPlan;
using universal_gnss_driver::HasSafeDispatchApproval;
using universal_gnss_driver::ReceiverAutoConfigApplyMode;
using universal_gnss_driver::ReceiverAutoConfigPlan;
using universal_gnss_driver::ReceiverAutoConfigPlanStatus;
using universal_gnss_driver::ReceiverAutoConfigProfile;
using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandKind;
using universal_gnss_driver::ReceiverCommandPayloadKind;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
using universal_gnss_driver::ReceiverDetectedFamily;

std::string ToLowerCopy(std::string value)
{
  for (char& c : value)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

std::string TrimTrailingCrLf(std::string text)
{
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
  {
    text.pop_back();
  }
  return text;
}

bool StartsWith(const std::string_view text, const std::string_view prefix)
{
  return text.size() >= prefix.size() && text.compare(0u, prefix.size(), prefix) == 0;
}

std::string EscapeJson(std::string_view text)
{
  std::ostringstream stream;
  for (const unsigned char c : text)
  {
    switch (c)
    {
      case '\\':
        stream << "\\\\";
        break;
      case '"':
        stream << "\\\"";
        break;
      case '\b':
        stream << "\\b";
        break;
      case '\f':
        stream << "\\f";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        if (c < 0x20u)
        {
          stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(c) << std::dec << std::setfill(' ');
        }
        else
        {
          stream << static_cast<char>(c);
        }
        break;
    }
  }
  return stream.str();
}

std::string FormatCompactDouble(const double value, const int precision = 3)
{
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(precision) << value;
  std::string text = stream.str();
  while (!text.empty() && text.back() == '0')
  {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.')
  {
    text.pop_back();
  }
  return text.empty() ? "0" : text;
}

const char* CommandKindToString(const ReceiverCommandKind kind)
{
  switch (kind)
  {
    case ReceiverCommandKind::kApplyConfigProfile:
      return "ApplyConfigProfile";
    case ReceiverCommandKind::kSetProtocolOutputs:
      return "SetProtocolOutputs";
    case ReceiverCommandKind::kQuery:
      return "Query";
    case ReceiverCommandKind::kRawBinary:
      return "RawBinary";
    case ReceiverCommandKind::kRawText:
      return "RawText";
    case ReceiverCommandKind::kReset:
      return "Reset";
    case ReceiverCommandKind::kUnknown:
      break;
  }

  return "Unknown";
}

const char* SafetyLevelToString(const ReceiverCommandSafetyLevel safety)
{
  switch (safety)
  {
    case ReceiverCommandSafetyLevel::kRuntime:
      return "runtime";
    case ReceiverCommandSafetyLevel::kPersistent:
      return "persistent";
    case ReceiverCommandSafetyLevel::kFactoryReset:
      return "factory_reset";
  }

  return "unknown";
}

const char* PayloadKindToString(const ReceiverCommandPayloadKind payload_kind)
{
  switch (payload_kind)
  {
    case ReceiverCommandPayloadKind::kBinary:
      return "binary";
    case ReceiverCommandPayloadKind::kText:
      return "text";
    case ReceiverCommandPayloadKind::kNone:
      break;
  }

  return "none";
}

std::size_t CommandPayloadSize(const ReceiverCommand& command)
{
  switch (command.payload.kind)
  {
    case ReceiverCommandPayloadKind::kBinary:
      return command.payload.binary.size();
    case ReceiverCommandPayloadKind::kText:
      return command.payload.text.size();
    case ReceiverCommandPayloadKind::kNone:
      break;
  }

  return 0u;
}

std::optional<std::uint32_t> ParsePlannedUnicoreConfigBaud(const ReceiverCommand& command)
{
  if (command.payload.kind != ReceiverCommandPayloadKind::kText)
  {
    return std::nullopt;
  }

  const std::string text = TrimTrailingCrLf(command.payload.text);
  constexpr std::string_view kPrefix = "CONFIG COM1 ";
  if (!StartsWith(text, kPrefix))
  {
    return std::nullopt;
  }

  const std::string_view remainder(text.data() + kPrefix.size(), text.size() - kPrefix.size());
  const std::size_t separator = remainder.find(' ');
  if (separator == std::string_view::npos || separator == 0u)
  {
    return std::nullopt;
  }

  try
  {
    std::size_t parsed = 0u;
    const auto baud =
        std::stoul(std::string(remainder.substr(0u, separator)), &parsed, 10);
    if (parsed != separator)
    {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(baud);
  }
  catch (...)
  {
    return std::nullopt;
  }
}

std::optional<std::uint32_t> ExtractPlannedUnicoreConfigBaud(
    const std::vector<ConfigPlanCommand>& commands)
{
  for (const auto& command : commands)
  {
    if (const auto baud = ParsePlannedUnicoreConfigBaud(command.command); baud.has_value())
    {
      return baud;
    }
  }

  return std::nullopt;
}

bool HasFactoryResetCommand(const std::vector<ConfigPlanCommand>& commands)
{
  for (const auto& command : commands)
  {
    if (command.command.kind == ReceiverCommandKind::kReset)
    {
      return true;
    }
  }

  return false;
}

std::optional<ReceiverDetectedFamily> ParseReceiverFamily(const std::string& vendor)
{
  const std::string normalized = ToLowerCopy(vendor);
  if (normalized == "ublox")
  {
    return ReceiverDetectedFamily::kUblox;
  }
  if (normalized == "unicore")
  {
    return ReceiverDetectedFamily::kUnicore;
  }
  if (normalized == "nmea")
  {
    return ReceiverDetectedFamily::kNmea;
  }

  return std::nullopt;
}

ConfigPlanStatus MapPlanStatus(const ReceiverAutoConfigPlanStatus status)
{
  switch (status)
  {
    case ReceiverAutoConfigPlanStatus::kOk:
      return ConfigPlanStatus::kOk;
    case ReceiverAutoConfigPlanStatus::kInvalidArgument:
      return ConfigPlanStatus::kInvalidArgument;
    case ReceiverAutoConfigPlanStatus::kUnsupportedReceiver:
      return ConfigPlanStatus::kUnsupportedReceiver;
    case ReceiverAutoConfigPlanStatus::kUnsupportedProfile:
      return ConfigPlanStatus::kUnsupportedProfile;
    case ReceiverAutoConfigPlanStatus::kUnsupportedApplyMode:
      return ConfigPlanStatus::kUnsupportedApplyMode;
    case ReceiverAutoConfigPlanStatus::kBuildError:
      return ConfigPlanStatus::kBuildError;
  }

  return ConfigPlanStatus::kBuildError;
}

ReceiverAutoConfigApplyMode ResolveApplyMode(const ConfigPlanOptions& options)
{
  return options.persistent ? ReceiverAutoConfigApplyMode::kPersistent
                            : ReceiverAutoConfigApplyMode::kRuntimeOnly;
}

std::string SelectErrorMessage(const ReceiverAutoConfigPlan& plan)
{
  if (!plan.error_message.empty())
  {
    return plan.error_message;
  }
  if (!plan.unsupported_reason.empty())
  {
    return plan.unsupported_reason;
  }
  return "configuration planning failed";
}

ConfigPlanResult BuildConfigPlanResultFromPlan(const ReceiverAutoConfigPlan& plan)
{
  ConfigPlanResult result;
  result.status = MapPlanStatus(plan.status);
  result.vendor = ToString(plan.request.receiver_family);
  result.receiver_family = plan.receiver_family_name;
  result.profile = universal_gnss_driver::ToString(plan.request.requested_profile);
  result.apply_mode = universal_gnss_driver::ToString(plan.request.apply_mode);
  result.persistent = plan.request.apply_mode == ReceiverAutoConfigApplyMode::kPersistent;
  result.signal_profile = plan.request.signal_profile;
  result.output_port = plan.request.output_port;
  result.resolved_output_port = plan.resolved_output_port;
  result.baud = plan.request.config_baud;
  result.rate_hz = plan.request.rate_hz;
  result.detected_device = plan.detected_device;
  result.detected_stable_id = plan.detected_stable_id;
  result.detected_baud = plan.detected_baud;
  if (plan.discovery_confidence.has_value())
  {
    result.discovery_confidence = universal_gnss_driver::ToString(*plan.discovery_confidence);
  }
  result.discovery_score = plan.discovery_score;
  result.receiver_recognized = plan.validation.receiver_recognized;
  result.config_supported = plan.validation.config_supported;
  result.profile_supported = plan.validation.profile_supported;
  result.apply_mode_supported = plan.validation.apply_mode_supported;
  result.production_ready = plan.validation.production_ready;
  result.ready_to_execute = plan.validation.ready_to_execute;
  result.warnings = plan.warnings;
  result.rollback_expectation = plan.rollback_expectation.summary;
  result.unsupported_reason = plan.unsupported_reason;
  result.error_message = SelectErrorMessage(plan);
  result.summary.commands_total = plan.validation.generated_command_count;
  result.summary.runtime_commands = plan.validation.runtime_command_count;
  result.summary.persistent_commands = plan.validation.persistent_command_count;
  result.summary.factory_reset_commands = plan.validation.factory_reset_command_count;

  if (plan.status != ReceiverAutoConfigPlanStatus::kOk)
  {
    return result;
  }

  for (const auto& planned_command : plan.commands)
  {
    ConfigPlanCommand command;
    command.command = planned_command;
    command.payload_bytes = CommandPayloadSize(planned_command);
    command.description = DescribeProfilePreviewCommand(planned_command);
    command.dispatch_safe_without_confirmation = HasSafeDispatchApproval(command.command);
    command.requires_explicit_safety_confirmation =
        !command.dispatch_safe_without_confirmation;

    if (command.requires_explicit_safety_confirmation)
    {
      ++result.summary.commands_requiring_confirmation;
      result.summary.requires_explicit_safety_confirmation = true;
    }

    result.commands.push_back(std::move(command));
  }

  return result;
}

}  // namespace

ConfigPlanResult BuildConfigPlan(const ConfigPlanOptions& options)
{
  ConfigPlanResult result;
  result.vendor = ToLowerCopy(options.vendor);
  result.profile = ToLowerCopy(options.profile);
  result.apply_mode = options.persistent ? "persistent" : "runtime_only";
  result.persistent = options.persistent;
  result.signal_profile = options.signal_profile;
  result.output_port = options.output_port;
  result.baud = options.baud;
  result.rate_hz = options.rate_hz;

  if (options.vendor.empty() || options.profile.empty())
  {
    result.status = ConfigPlanStatus::kInvalidArgument;
    result.error_message = "both vendor and profile are required";
    return result;
  }

  const auto family = ParseReceiverFamily(options.vendor);
  if (!family.has_value())
  {
    result.status = ConfigPlanStatus::kUnsupportedReceiver;
    result.error_message = "unsupported receiver family";
    result.unsupported_reason = result.error_message;
    return result;
  }

  const auto profile = universal_gnss_driver::ParseReceiverAutoConfigProfile(options.profile);
  if (!profile.has_value())
  {
    result.status = ConfigPlanStatus::kUnsupportedProfile;
    result.error_message = "unsupported configuration profile";
    result.unsupported_reason = result.error_message;
    return result;
  }

  universal_gnss_driver::ReceiverAutoConfigRequest request;
  request.receiver_family = *family;
  request.requested_profile = *profile;
  request.apply_mode = ResolveApplyMode(options);
  request.signal_profile = options.signal_profile;
  request.output_port = options.output_port;
  request.config_baud = options.baud;
  request.rate_hz = options.rate_hz;

  result = BuildConfigPlan(request);
  result.vendor = ToLowerCopy(options.vendor);
  return result;
}

ConfigPlanResult BuildConfigPlan(
    const universal_gnss_driver::ReceiverAutoConfigRequest& request)
{
  return BuildConfigPlan(BuildReceiverAutoConfigPlan(request));
}

ConfigPlanResult BuildConfigPlan(
    const universal_gnss_driver::ReceiverAutoConfigPlan& plan)
{
  return BuildConfigPlanResultFromPlan(plan);
}

std::string FormatConfigPlanText(const ConfigPlanResult& result)
{
  std::ostringstream output;
  if (result.status != ConfigPlanStatus::kOk)
  {
    output << "Error: " << result.error_message << '\n';
    return output.str();
  }

  output << "Receiver family: " << result.receiver_family << "\n";
  output << "Profile: " << result.vendor << ' ' << result.profile << "\n";
  output << "Apply mode: " << result.apply_mode << "\n";
  output << "Dry run: yes\n";
  if (result.detected_device.has_value())
  {
    output << "Detected device: " << *result.detected_device << "\n";
  }
  if (result.detected_stable_id.has_value())
  {
    output << "Detected stable id: " << *result.detected_stable_id << "\n";
  }
  if (result.detected_baud.has_value())
  {
    output << "Current transport baud: " << *result.detected_baud << "\n";
  }
  if (result.discovery_confidence.has_value())
  {
    output << "Discovery confidence: " << *result.discovery_confidence << "\n";
  }
  if (result.discovery_score.has_value())
  {
    output << "Discovery score: " << *result.discovery_score << "\n";
  }
  output << "Safety confirmation required: "
         << (result.summary.requires_explicit_safety_confirmation ? "yes" : "no") << "\n";
  output << "Production ready: " << (result.production_ready ? "yes" : "no") << "\n";
  output << "Ready to execute later: " << (result.ready_to_execute ? "yes" : "no") << "\n";
  output << "Command count: " << result.summary.commands_total << "\n";
  output << "Runtime commands: " << result.summary.runtime_commands << "\n";
  output << "Persistent commands: " << result.summary.persistent_commands << "\n";
  output << "Factory-reset commands: " << result.summary.factory_reset_commands << "\n";
  output << "Commands requiring confirmation: "
         << result.summary.commands_requiring_confirmation << "\n";
  if (result.persistent)
  {
    output << "Persistence request: enabled\n";
  }
  if (result.signal_profile.has_value())
  {
    output << "Signal profile override: "
           << universal_gnss_driver::ToString(*result.signal_profile) << "\n";
  }
  if (result.vendor == "ublox")
  {
    if (!result.output_port.has_value())
    {
      output << "Output port: legacy_default (uart1 + usb)\n";
    }
    else if (*result.output_port == universal_gnss_driver::ReceiverAutoConfigOutputPort::kAuto)
    {
      output << "Output port request: auto\n";
      if (result.resolved_output_port.has_value())
      {
        output << "Resolved output port: "
               << universal_gnss_driver::ToString(*result.resolved_output_port) << "\n";
      }
    }
    else
    {
      output << "Output port: "
             << universal_gnss_driver::ToString(
                    result.resolved_output_port.value_or(*result.output_port))
             << "\n";
    }
  }
  if (result.baud.has_value())
  {
    output << "Config baud override: " << *result.baud << "\n";
  }
  if (HasFactoryResetCommand(result.commands))
  {
    output << "Factory reset baud: 115200\n";
  }
  if (const auto target_baud = ExtractPlannedUnicoreConfigBaud(result.commands);
      target_baud.has_value())
  {
    output << "Target configured baud: " << *target_baud << "\n";
  }
  if (result.rate_hz.has_value())
  {
    output << "Rate override: " << FormatCompactDouble(*result.rate_hz) << " Hz\n";
  }
  output << "\nCommand sequence:\n";

  for (std::size_t index = 0; index < result.commands.size(); ++index)
  {
    const auto& command = result.commands[index];
    output << '\n' << (index + 1u) << ". "
           << CommandKindToString(command.command.kind)
           << " [" << SafetyLevelToString(command.command.safety_level);
    if (command.requires_explicit_safety_confirmation)
    {
      output << ", confirmation_required";
    }
    output << "]\n";
    output << "   payload: " << PayloadKindToString(command.command.payload.kind)
           << ", " << command.payload_bytes << " bytes\n";
    output << "   description: " << command.description << "\n";

    if (command.command.payload.kind == ReceiverCommandPayloadKind::kText)
    {
      output << "   command: "
             << TrimTrailingCrLf(command.command.payload.text) << "\n";
    }
  }

  if (!result.warnings.empty())
  {
    output << "\nWarnings:\n";
    for (const auto& warning : result.warnings)
    {
      output << "- " << warning << "\n";
    }
  }

  if (!result.rollback_expectation.empty())
  {
    output << "\nRollback expectation:\n";
    output << result.rollback_expectation << "\n";
  }

  return output.str();
}

std::string FormatConfigPlanJson(const ConfigPlanResult& result)
{
  std::ostringstream output;
  output << "{\n";
  output << "  \"status\": \""
         << (result.status == ConfigPlanStatus::kOk ? "ok" : "error") << "\",\n";
  output << "  \"dry_run\": true,\n";
  output << "  \"profile\": {\n";
  output << "    \"vendor\": \"" << EscapeJson(result.vendor) << "\",\n";
  output << "    \"receiver_family\": \"" << EscapeJson(result.receiver_family) << "\",\n";
  output << "    \"name\": \"" << EscapeJson(result.profile) << "\",\n";
  output << "    \"apply_mode\": \"" << EscapeJson(result.apply_mode) << "\",\n";
  output << "    \"persistent\": " << (result.persistent ? "true" : "false") << ",\n";
  output << "    \"signal_profile\": ";
  if (result.signal_profile.has_value())
  {
    output << "\"" << EscapeJson(universal_gnss_driver::ToString(*result.signal_profile))
           << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"output_port\": ";
  if (result.output_port.has_value())
  {
    output << "\"" << EscapeJson(universal_gnss_driver::ToString(*result.output_port))
           << "\"";
  }
  else if (result.vendor == "ublox")
  {
    output << "\"legacy_default\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"resolved_output_port\": ";
  if (result.resolved_output_port.has_value())
  {
    output << "\""
           << EscapeJson(universal_gnss_driver::ToString(*result.resolved_output_port))
           << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"baud\": ";
  if (result.baud.has_value())
  {
    output << *result.baud;
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"target_configured_baud\": ";
  if (const auto target_baud = ExtractPlannedUnicoreConfigBaud(result.commands);
      target_baud.has_value())
  {
    output << *target_baud;
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"factory_reset_baud\": ";
  if (HasFactoryResetCommand(result.commands))
  {
    output << 115200u;
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"rate_hz\": ";
  if (result.rate_hz.has_value())
  {
    output << FormatCompactDouble(*result.rate_hz, 6);
  }
  else
  {
    output << "null";
  }
  output << "\n";
  output << "  },\n";
  output << "  \"discovery\": {\n";
  output << "    \"device\": ";
  if (result.detected_device.has_value())
  {
    output << "\"" << EscapeJson(*result.detected_device) << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"stable_id\": ";
  if (result.detected_stable_id.has_value())
  {
    output << "\"" << EscapeJson(*result.detected_stable_id) << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"baud\": ";
  if (result.detected_baud.has_value())
  {
    output << *result.detected_baud;
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"confidence\": ";
  if (result.discovery_confidence.has_value())
  {
    output << "\"" << EscapeJson(*result.discovery_confidence) << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"score\": ";
  if (result.discovery_score.has_value())
  {
    output << *result.discovery_score;
  }
  else
  {
    output << "null";
  }
  output << "\n";
  output << "  },\n";
  output << "  \"validation\": {\n";
  output << "    \"receiver_recognized\": "
         << (result.receiver_recognized ? "true" : "false") << ",\n";
  output << "    \"config_supported\": "
         << (result.config_supported ? "true" : "false") << ",\n";
  output << "    \"profile_supported\": "
         << (result.profile_supported ? "true" : "false") << ",\n";
  output << "    \"apply_mode_supported\": "
         << (result.apply_mode_supported ? "true" : "false") << ",\n";
  output << "    \"production_ready\": "
         << (result.production_ready ? "true" : "false") << ",\n";
  output << "    \"ready_to_execute\": "
         << (result.ready_to_execute ? "true" : "false") << "\n";
  output << "  },\n";
  output << "  \"summary\": {\n";
  output << "    \"commands\": " << result.summary.commands_total << ",\n";
  output << "    \"runtime\": " << result.summary.runtime_commands << ",\n";
  output << "    \"persistent\": " << result.summary.persistent_commands << ",\n";
  output << "    \"factory_reset\": " << result.summary.factory_reset_commands << ",\n";
  output << "    \"commands_requiring_confirmation\": "
         << result.summary.commands_requiring_confirmation << ",\n";
  output << "    \"safety_confirmation_required\": "
         << (result.summary.requires_explicit_safety_confirmation ? "true" : "false") << "\n";
  output << "  },\n";
  output << "  \"commands\": [\n";

  for (std::size_t index = 0; index < result.commands.size(); ++index)
  {
    const auto& command = result.commands[index];
    output << "    {\n";
    output << "      \"index\": " << (index + 1u) << ",\n";
    output << "      \"kind\": \"" << CommandKindToString(command.command.kind) << "\",\n";
    output << "      \"safety\": \"" << SafetyLevelToString(command.command.safety_level)
           << "\",\n";
    output << "      \"payload_kind\": \""
           << PayloadKindToString(command.command.payload.kind) << "\",\n";
    output << "      \"bytes\": " << command.payload_bytes << ",\n";
    output << "      \"description\": \"" << EscapeJson(command.description) << "\",\n";
    output << "      \"requires_confirmation\": "
           << (command.requires_explicit_safety_confirmation ? "true" : "false") << ",\n";
    output << "      \"dispatch_safe\": "
           << (command.dispatch_safe_without_confirmation ? "true" : "false");

    if (command.command.payload.kind == ReceiverCommandPayloadKind::kText)
    {
      output << ",\n      \"command\": \""
             << EscapeJson(TrimTrailingCrLf(command.command.payload.text)) << "\"";
    }

    output << "\n    }";
    if (index + 1u != result.commands.size())
    {
      output << ",";
    }
    output << "\n";
  }

  output << "  ],\n";
  output << "  \"warnings\": [\n";
  for (std::size_t index = 0; index < result.warnings.size(); ++index)
  {
    output << "    \"" << EscapeJson(result.warnings[index]) << "\"";
    if (index + 1u != result.warnings.size())
    {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"rollback_expectation\": \""
         << EscapeJson(result.rollback_expectation) << "\",\n";
  output << "  \"unsupported_reason\": \"" << EscapeJson(result.unsupported_reason) << "\",\n";
  output << "  \"error_message\": \"" << EscapeJson(result.error_message) << "\"\n";
  output << "}\n";
  return output.str();
}

}  // namespace universal_gnss_tools
