#include "universal_gnss_tools/config_plan.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace universal_gnss_tools
{

namespace
{

using universal_gnss_driver::HasSafeDispatchApproval;
using universal_gnss_driver::ReceiverCommandKind;
using universal_gnss_driver::ReceiverCommandPayloadKind;
using universal_gnss_driver::ReceiverCommandSafetyLevel;

std::string TrimTrailingCrLf(std::string text)
{
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
  {
    text.pop_back();
  }
  return text;
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

std::string ResolveReceiverFamily(const ProfilePreviewResult& preview)
{
  if (!preview.commands.empty() && !preview.commands.front().command.target.family.empty())
  {
    return std::string(preview.commands.front().command.target.family);
  }

  if (preview.vendor == "ublox")
  {
    return "F9/F10";
  }

  if (preview.vendor == "unicore")
  {
    return "UM98x";
  }

  return {};
}

}  // namespace

ConfigPlanResult BuildConfigPlan(const ConfigPlanOptions& options)
{
  ProfilePreviewOptions preview_options;
  preview_options.vendor = options.vendor;
  preview_options.profile = options.profile;
  preview_options.persistent = options.persistent;
  preview_options.baud = options.baud;
  preview_options.rate_hz = options.rate_hz;

  const ProfilePreviewResult preview = BuildProfilePreview(preview_options);

  ConfigPlanResult result;
  result.status = preview.status;
  result.vendor = preview.vendor;
  result.receiver_family = ResolveReceiverFamily(preview);
  result.profile = preview.profile;
  result.persistent = preview.persistent;
  result.baud = preview.baud;
  result.rate_hz = preview.rate_hz;
  result.error_message = preview.error_message;
  result.summary.commands_total = preview.summary.commands_total;
  result.summary.runtime_commands = preview.summary.runtime_commands;
  result.summary.persistent_commands = preview.summary.persistent_commands;
  result.summary.factory_reset_commands = preview.summary.factory_reset_commands;

  if (preview.status != ProfilePreviewStatus::kOk)
  {
    return result;
  }

  for (const auto& preview_command : preview.commands)
  {
    ConfigPlanCommand command;
    command.command = preview_command.command;
    command.payload_bytes = preview_command.payload_bytes;
    command.description = preview_command.description;
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
  output << "Dry run: yes\n";
  output << "Safety confirmation required: "
         << (result.summary.requires_explicit_safety_confirmation ? "yes" : "no") << "\n";
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
  if (result.baud.has_value())
  {
    output << "Baud override: " << *result.baud << "\n";
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
  output << "    \"persistent\": " << (result.persistent ? "true" : "false") << ",\n";
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
  output << "  \"error_message\": \"" << EscapeJson(result.error_message) << "\"\n";
  output << "}\n";
  return output.str();
}

}  // namespace universal_gnss_tools
