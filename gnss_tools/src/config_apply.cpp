#include "universal_gnss_tools/config_apply.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_command_response.hpp"
#include "universal_gnss_driver/receiver_config_application.hpp"
#include "universal_gnss_driver/ublox_response_router.hpp"
#include "universal_gnss_driver/unicore_response_router.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/transport_status.hpp"

namespace universal_gnss_tools
{

namespace
{

using universal_gnss_driver::HasSafeDispatchApproval;
using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandPayloadKind;
using universal_gnss_driver::ReceiverCommandResponse;
using universal_gnss_driver::ReceiverCommandResponseKind;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
using universal_gnss_driver::ReceiverConfigApplication;
using universal_gnss_driver::ReceiverConfigApplicationConfig;
using universal_gnss_driver::ReceiverConfigApplicationResult;
using universal_gnss_driver::ReceiverConfigApplicationState;
using universal_gnss_driver::ReceiverCommandResponseMatchMetadata;
using universal_gnss_driver::ReceiverCommandTimestampNs;
using universal_gnss_driver::UbloxResponseRouter;
using universal_gnss_driver::UbloxRoutedResponse;
using universal_gnss_driver::UnicoreResponseRouter;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::ProtocolTimestampNs;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_transport::ByteDuplex;
using universal_gnss_transport::TransportStatus;

using Clock = std::chrono::steady_clock;

ConfigApplyStatus MapPlanStatus(const ConfigPlanStatus status)
{
  switch (status)
  {
    case ConfigPlanStatus::kOk:
      return ConfigApplyStatus::kOk;
    case ConfigPlanStatus::kInvalidArgument:
      return ConfigApplyStatus::kInvalidArgument;
    case ConfigPlanStatus::kUnsupportedReceiver:
      return ConfigApplyStatus::kUnsupportedReceiver;
    case ConfigPlanStatus::kUnsupportedProfile:
      return ConfigApplyStatus::kUnsupportedProfile;
    case ConfigPlanStatus::kUnsupportedApplyMode:
      return ConfigApplyStatus::kInvalidArgument;
    case ConfigPlanStatus::kBuildError:
      return ConfigApplyStatus::kBuildError;
  }

  return ConfigApplyStatus::kApplicationFailed;
}

const char* ToString(const ConfigApplyStatus status)
{
  switch (status)
  {
    case ConfigApplyStatus::kOk:
      return "ok";
    case ConfigApplyStatus::kInvalidArgument:
      return "invalid_argument";
    case ConfigApplyStatus::kUnsupportedReceiver:
      return "unsupported_receiver";
    case ConfigApplyStatus::kUnsupportedVendor:
      return "unsupported_vendor";
    case ConfigApplyStatus::kUnsupportedProfile:
      return "unsupported_profile";
    case ConfigApplyStatus::kBuildError:
      return "build_error";
    case ConfigApplyStatus::kSafetyRejected:
      return "safety_rejected";
    case ConfigApplyStatus::kTransportUnavailable:
      return "transport_unavailable";
    case ConfigApplyStatus::kReadFailed:
      return "read_failed";
    case ConfigApplyStatus::kDispatchFailed:
      return "dispatch_failed";
    case ConfigApplyStatus::kRejected:
      return "rejected";
    case ConfigApplyStatus::kTimedOut:
      return "timed_out";
    case ConfigApplyStatus::kApplicationFailed:
      return "application_failed";
  }

  return "application_failed";
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

std::string TrimTrailingCrLf(std::string text)
{
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
  {
    text.pop_back();
  }
  return text;
}

const char* CommandKindToString(const universal_gnss_driver::ReceiverCommandKind kind)
{
  switch (kind)
  {
    case universal_gnss_driver::ReceiverCommandKind::kApplyConfigProfile:
      return "ApplyConfigProfile";
    case universal_gnss_driver::ReceiverCommandKind::kSetProtocolOutputs:
      return "SetProtocolOutputs";
    case universal_gnss_driver::ReceiverCommandKind::kQuery:
      return "Query";
    case universal_gnss_driver::ReceiverCommandKind::kRawBinary:
      return "RawBinary";
    case universal_gnss_driver::ReceiverCommandKind::kRawText:
      return "RawText";
    case universal_gnss_driver::ReceiverCommandKind::kReset:
      return "Reset";
    case universal_gnss_driver::ReceiverCommandKind::kUnknown:
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

ReceiverCommandTimestampNs NowTimestampNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}

ConfigApplyResult MakeBaseResult(const ConfigApplyOptions& options)
{
  ConfigApplyResult result;
  result.execute_requested = options.execute;
  result.dry_run = !options.execute;
  result.executed = false;
  result.port = options.port;
  result.transport_baud_rate = options.transport_baud_rate;
  result.timeout_ms = options.timeout_ms;
  return result;
}

void PopulateExecutionSummaryFromPlan(ConfigApplyResult& result)
{
  result.execution_summary.commands_total = result.plan.summary.commands_total;
}

bool ApplyExecutionSafetyRules(ConfigApplyResult& result,
                               const ConfigApplyOptions& options)
{
  result.requires_runtime_confirmation = result.plan.summary.runtime_commands > 0u;
  result.requires_persistent_confirmation =
      result.plan.summary.persistent_commands > 0u ||
      result.plan.summary.factory_reset_commands > 0u;
  result.execution_confirmed =
      (!result.requires_runtime_confirmation || options.confirm_runtime) &&
      (!result.requires_persistent_confirmation || options.confirm_persistent);

  if (result.plan.summary.factory_reset_commands > 0u)
  {
    result.status = ConfigApplyStatus::kSafetyRejected;
    result.error_message =
        "factory-reset commands are not supported by gnss_config_apply";
    result.execution_summary.final_status = "safety_rejected";
    return false;
  }

  if (!options.execute)
  {
    result.execution_summary.final_status = "dry_run";
    return true;
  }

  if (result.execution_confirmed)
  {
    return true;
  }

  result.status = ConfigApplyStatus::kSafetyRejected;
  result.error_message.clear();
  if (result.requires_runtime_confirmation && !options.confirm_runtime)
  {
    result.error_message +=
        "execute mode requires --confirm-runtime for runtime commands";
  }
  if (result.requires_persistent_confirmation && !options.confirm_persistent)
  {
    if (!result.error_message.empty())
    {
      result.error_message += "; ";
    }
    result.error_message +=
        "execute mode requires --confirm-persistent for persistent or factory-reset commands";
  }
  result.execution_summary.final_status = "safety_rejected";
  return false;
}

std::vector<ReceiverCommand> BuildExecutableCommands(const ConfigPlanResult& plan,
                                                     const ConfigApplyOptions& options)
{
  std::vector<ReceiverCommand> commands;
  commands.reserve(plan.commands.size());

  for (const auto& plan_command : plan.commands)
  {
    ReceiverCommand command = plan_command.command;
    command.retry_policy.timeout_ms = options.timeout_ms;

    if (command.safety_level == ReceiverCommandSafetyLevel::kPersistent ||
        command.safety_level == ReceiverCommandSafetyLevel::kFactoryReset)
    {
      command.explicit_safety_confirmation = options.confirm_persistent;
    }

    commands.push_back(std::move(command));
  }

  return commands;
}

std::string MakeCommandProgressPrefix(const std::size_t index,
                                      const std::size_t total,
                                      const char* action)
{
  std::ostringstream stream;
  stream << "Command " << index << "/" << total << ": " << action;
  return stream.str();
}

void NoteApplicationProgress(ConfigApplyResult& result,
                             const ConfigPlanResult& plan,
                             const ReceiverConfigApplicationResult& application_result)
{
  const std::size_t total = plan.commands.size();
  const std::size_t display_index =
      application_result.command_index < total ? (application_result.command_index + 1u)
                                               : total;

  if (application_result.command_started && application_result.command_index < total)
  {
    const auto& command = plan.commands[application_result.command_index];
    result.progress_log.push_back(
        MakeCommandProgressPrefix(display_index, total, "dispatching") +
        " - " + command.description);
  }

  if (application_result.retry_dispatched && application_result.command_index < total)
  {
    const auto& command = plan.commands[application_result.command_index];
    result.progress_log.push_back(
        MakeCommandProgressPrefix(display_index, total, "retrying") +
        " - " + command.description);
  }

  if (application_result.command_finished)
  {
    std::size_t plan_index = application_result.command_index;
    if (application_result.advanced_to_next_command && application_result.command_index > 0u)
    {
      plan_index = application_result.command_index - 1u;
    }
    else if (application_result.state == ReceiverConfigApplicationState::kCompleted &&
             total > 0u)
    {
      plan_index = total - 1u;
    }

    if (plan_index >= total)
    {
      plan_index = total > 0u ? (total - 1u) : 0u;
    }

    std::string suffix;
    if (!application_result.error_message.empty())
    {
      suffix = ": " + application_result.error_message;
    }
    else if (application_result.engine_result.has_value() &&
             !application_result.engine_result->error_message.empty())
    {
      suffix = ": " + application_result.engine_result->error_message;
    }

    const bool succeeded =
        application_result.state == ReceiverConfigApplicationState::kRunning ||
        application_result.state == ReceiverConfigApplicationState::kCompleted;

    result.progress_log.push_back(
        MakeCommandProgressPrefix(
            plan_index + 1u,
            total,
            succeeded ? "completed" : "failed") +
        " - " + plan.commands[plan_index].description + suffix);
  }
}

ConfigApplyStatus MapApplicationFailureStatus(
    const ReceiverConfigApplicationResult& application_result)
{
  if (application_result.engine_result.has_value())
  {
    switch (application_result.engine_result->status)
    {
      case universal_gnss_driver::ReceiverCommandTransactionEngineStepStatus::kDispatchFailed:
        return ConfigApplyStatus::kDispatchFailed;
      case universal_gnss_driver::ReceiverCommandTransactionEngineStepStatus::kRejected:
        return ConfigApplyStatus::kRejected;
      case universal_gnss_driver::ReceiverCommandTransactionEngineStepStatus::kTimedOut:
      case universal_gnss_driver::ReceiverCommandTransactionEngineStepStatus::kRetryUnavailable:
        return ConfigApplyStatus::kTimedOut;
      default:
        break;
    }
  }

  return ConfigApplyStatus::kApplicationFailed;
}

bool FinalizeIfApplicationStopped(ConfigApplyResult& result,
                                  const ReceiverConfigApplication& application,
                                  const ReceiverConfigApplicationResult& application_result)
{
  if (application.state() == ReceiverConfigApplicationState::kCompleted)
  {
    result.status = ConfigApplyStatus::kOk;
    result.execution_summary.final_status = "completed";
    result.error_message.clear();
    return true;
  }

  if (application.state() == ReceiverConfigApplicationState::kFailed)
  {
    result.status = MapApplicationFailureStatus(application_result);
    result.execution_summary.final_status = ToString(result.status);
    result.error_message = !application_result.error_message.empty()
                               ? application_result.error_message
                               : (application_result.engine_result.has_value()
                                      ? application_result.engine_result->error_message
                                      : std::string{"configuration apply failed"});
    return true;
  }

  (void)application;
  return false;
}

void UpdateExecutionSummary(ConfigApplyResult& result,
                            const ReceiverConfigApplication& application)
{
  result.execution_summary.commands_total = application.metrics().commands_total;
  result.execution_summary.commands_completed = application.metrics().commands_completed;
  result.execution_summary.commands_failed = application.metrics().commands_failed;
  result.execution_summary.commands_retried = application.metrics().commands_retried;
  result.execution_summary.responses_applied = application.metrics().responses_applied;
}

bool ApplyQueuedUbloxResponses(ConfigApplyResult& result,
                               const ConfigPlanResult& plan,
                               ReceiverConfigApplication& application,
                               UbloxResponseRouter& router)
{
  while (application.state() == ReceiverConfigApplicationState::kWaitingForResponse &&
         router.pending_response_count() > 0u)
  {
    UbloxRoutedResponse routed_response;
    if (!router.PopResponse(routed_response))
    {
      break;
    }

    ReceiverCommandResponseMatchMetadata match_metadata;
    match_metadata.ubx_target = routed_response.ubx_target;
    const auto application_result =
        application.ApplyResponse(routed_response.response, match_metadata);
    NoteApplicationProgress(result, plan, application_result);

    if (FinalizeIfApplicationStopped(result, application, application_result))
    {
      return true;
    }
  }

  return false;
}

bool ApplyQueuedUnicoreResponses(ConfigApplyResult& result,
                                 const ConfigPlanResult& plan,
                                 ReceiverConfigApplication& application,
                                 UnicoreResponseRouter& router)
{
  while (application.state() == ReceiverConfigApplicationState::kWaitingForResponse &&
         router.pending_response_count() > 0u)
  {
    ReceiverCommandResponse response;
    if (!router.PopResponse(response))
    {
      break;
    }

    const auto application_result = application.ApplyResponse(response);
    NoteApplicationProgress(result, plan, application_result);

    if (FinalizeIfApplicationStopped(result, application, application_result))
    {
      return true;
    }
  }

  return false;
}

bool ProcessUbloxBytes(ConfigApplyResult& result,
                       const ConfigPlanResult& plan,
                       ReceiverConfigApplication& application,
                       UbxFrameFramer& framer,
                       UbloxResponseRouter& router,
                       const std::uint8_t* data,
                       const std::size_t size,
                       const ProtocolTimestampNs timestamp_ns)
{
  for (std::size_t index = 0; index < size; ++index)
  {
    const auto framed = framer.PushByte(data[index], timestamp_ns);
    if (framed.status == ParserStatus::kRecordReady && framed.record.has_value())
    {
      router.ProcessUbxFrame(*framed.record);
      if (ApplyQueuedUbloxResponses(result, plan, application, router))
      {
        return true;
      }
    }
  }

  return false;
}

bool ProcessUnicoreBytes(ConfigApplyResult& result,
                         const ConfigPlanResult& plan,
                         ReceiverConfigApplication& application,
                         UnicoreResponseRouter& router,
                         const std::uint8_t* data,
                         const std::size_t size,
                         const ProtocolTimestampNs timestamp_ns)
{
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  router.FeedBytes(text, timestamp_ns);
  return ApplyQueuedUnicoreResponses(result, plan, application, router);
}

ConfigPlanOptions MakePlanOptions(const ConfigApplyOptions& options)
{
  ConfigPlanOptions plan_options;
  plan_options.vendor = options.vendor;
  plan_options.profile = options.profile;
  plan_options.persistent = options.persistent;
  plan_options.rate_hz = options.rate_hz;
  return plan_options;
}

void AppendCommandSequenceText(std::ostringstream& output,
                               const ConfigApplyResult& result)
{
  output << "Command sequence:\n";
  for (std::size_t index = 0; index < result.plan.commands.size(); ++index)
  {
    const auto& command = result.plan.commands[index];
    output << '\n' << (index + 1u) << ". "
           << CommandKindToString(command.command.kind)
           << " [" << SafetyLevelToString(command.command.safety_level);
    if (command.requires_explicit_safety_confirmation)
    {
      output << ", dispatcher_confirmation_required";
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
}

void AppendCommandSequenceJson(std::ostringstream& output,
                               const ConfigApplyResult& result)
{
  output << "  \"commands\": [\n";
  for (std::size_t index = 0; index < result.plan.commands.size(); ++index)
  {
    const auto& command = result.plan.commands[index];
    output << "    {\n";
    output << "      \"index\": " << (index + 1u) << ",\n";
    output << "      \"kind\": \"" << CommandKindToString(command.command.kind) << "\",\n";
    output << "      \"safety\": \"" << SafetyLevelToString(command.command.safety_level)
           << "\",\n";
    output << "      \"payload_kind\": \""
           << PayloadKindToString(command.command.payload.kind) << "\",\n";
    output << "      \"bytes\": " << command.payload_bytes << ",\n";
    output << "      \"description\": \"" << EscapeJson(command.description) << "\",\n";
    output << "      \"dispatcher_confirmation_required\": "
           << (command.requires_explicit_safety_confirmation ? "true" : "false");
    if (command.command.payload.kind == ReceiverCommandPayloadKind::kText)
    {
      output << ",\n      \"command\": \""
             << EscapeJson(TrimTrailingCrLf(command.command.payload.text)) << "\"";
    }
    output << "\n    }";
    if (index + 1u != result.plan.commands.size())
    {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
}

}  // namespace

ConfigApplyResult PrepareConfigApply(const ConfigApplyOptions& options)
{
  ConfigApplyResult result = MakeBaseResult(options);

  if (options.vendor.empty() || options.profile.empty())
  {
    result.status = ConfigApplyStatus::kInvalidArgument;
    result.error_message = "both vendor and profile are required";
    result.execution_summary.final_status = ToString(result.status);
    return result;
  }

  if (options.timeout_ms == 0u)
  {
    result.status = ConfigApplyStatus::kInvalidArgument;
    result.error_message = "timeout-ms must be non-zero";
    result.execution_summary.final_status = ToString(result.status);
    return result;
  }

  result.plan = BuildConfigPlan(MakePlanOptions(options));
  result.status = MapPlanStatus(result.plan.status);
  PopulateExecutionSummaryFromPlan(result);

  if (result.status != ConfigApplyStatus::kOk)
  {
    result.error_message = result.plan.error_message;
    result.execution_summary.final_status = ToString(result.status);
    return result;
  }

  if (!ApplyExecutionSafetyRules(result, options))
  {
    return result;
  }

  result.status = ConfigApplyStatus::kOk;
  return result;
}

ConfigApplyResult ExecuteConfigApply(ByteDuplex& transport,
                                     const ConfigApplyOptions& options)
{
  ConfigApplyResult result = PrepareConfigApply(options);
  if (result.status != ConfigApplyStatus::kOk || !options.execute)
  {
    return result;
  }

  if (!static_cast<universal_gnss_transport::ByteSource&>(transport).IsOpen())
  {
    result.status = ConfigApplyStatus::kTransportUnavailable;
    result.error_message = "transport must be open before execution";
    result.execution_summary.final_status = ToString(result.status);
    return result;
  }

  result.dry_run = false;
  result.executed = true;

  ReceiverConfigApplicationConfig application_config;
  ReceiverConfigApplication application(transport, application_config);
  UbxFrameFramer ubx_framer;
  UbloxResponseRouter ublox_router;
  UnicoreResponseRouter unicore_router;

  std::vector<ReceiverCommand> commands = BuildExecutableCommands(result.plan, options);
  auto application_result = application.Start(commands, NowTimestampNs());
  NoteApplicationProgress(result, result.plan, application_result);
  UpdateExecutionSummary(result, application);

  if (FinalizeIfApplicationStopped(result, application, application_result))
  {
    UpdateExecutionSummary(result, application);
    return result;
  }

  std::vector<std::uint8_t> read_buffer(256u, 0u);

  while (application.state() == ReceiverConfigApplicationState::kRunning ||
         application.state() == ReceiverConfigApplicationState::kWaitingForResponse)
  {
    if (application.state() == ReceiverConfigApplicationState::kRunning)
    {
      application_result = application.Step(NowTimestampNs());
      NoteApplicationProgress(result, result.plan, application_result);
      UpdateExecutionSummary(result, application);
      if (FinalizeIfApplicationStopped(result, application, application_result))
      {
        break;
      }
    }

    if (application.state() != ReceiverConfigApplicationState::kWaitingForResponse)
    {
      continue;
    }

    if (result.plan.vendor == "ublox" &&
        ApplyQueuedUbloxResponses(result, result.plan, application, ublox_router))
    {
      UpdateExecutionSummary(result, application);
      break;
    }

    if (result.plan.vendor == "unicore" &&
        ApplyQueuedUnicoreResponses(result, result.plan, application, unicore_router))
    {
      UpdateExecutionSummary(result, application);
      break;
    }

    const auto read_result = transport.Read(read_buffer.data(), read_buffer.size());
    if (read_result.status == TransportStatus::kError ||
        read_result.status == TransportStatus::kClosed)
    {
      result.status = ConfigApplyStatus::kReadFailed;
      result.error_message = "transport read failed while waiting for a response";
      result.execution_summary.final_status = ToString(result.status);
      result.progress_log.push_back("Read failed while waiting for receiver response");
      UpdateExecutionSummary(result, application);
      break;
    }

    if (read_result.bytes_read > 0u)
    {
      const auto timestamp_ns = NowTimestampNs();
      const bool stopped =
          result.plan.vendor == "ublox"
              ? ProcessUbloxBytes(result,
                                 result.plan,
                                 application,
                                 ubx_framer,
                                 ublox_router,
                                 read_buffer.data(),
                                 read_result.bytes_read,
                                 timestamp_ns)
              : ProcessUnicoreBytes(result,
                                    result.plan,
                                    application,
                                    unicore_router,
                                    read_buffer.data(),
                                    read_result.bytes_read,
                                    timestamp_ns);

      UpdateExecutionSummary(result, application);
      if (stopped)
      {
        break;
      }
    }

    application_result = application.CheckTimeout(NowTimestampNs());
    NoteApplicationProgress(result, result.plan, application_result);
    UpdateExecutionSummary(result, application);
    if (FinalizeIfApplicationStopped(result, application, application_result))
    {
      break;
    }
  }

  if (result.execution_summary.final_status.empty())
  {
    UpdateExecutionSummary(result, application);
    result.execution_summary.final_status =
        application.state() == ReceiverConfigApplicationState::kCompleted ? "completed"
                                                                          : "application_failed";
  }

  return result;
}

std::string FormatConfigApplyText(const ConfigApplyResult& result)
{
  std::ostringstream output;
  output << "Receiver family: " << result.plan.receiver_family << "\n";
  output << "Profile: " << result.plan.vendor << ' ' << result.plan.profile << "\n";
  output << "Dry run: " << (result.dry_run ? "yes" : "no") << "\n";
  output << "Execute requested: " << (result.execute_requested ? "yes" : "no") << "\n";
  output << "Executed: " << (result.executed ? "yes" : "no") << "\n";
  output << "Runtime confirmation required: "
         << (result.requires_runtime_confirmation ? "yes" : "no") << "\n";
  output << "Persistent confirmation required: "
         << (result.requires_persistent_confirmation ? "yes" : "no") << "\n";
  output << "Execution confirmed: " << (result.execution_confirmed ? "yes" : "no") << "\n";
  output << "Command count: " << result.plan.summary.commands_total << "\n";
  output << "Runtime commands: " << result.plan.summary.runtime_commands << "\n";
  output << "Persistent commands: " << result.plan.summary.persistent_commands << "\n";
  output << "Factory-reset commands: " << result.plan.summary.factory_reset_commands << "\n";
  if (!result.port.empty())
  {
    output << "Port: " << result.port << "\n";
  }
  if (result.transport_baud_rate != 0u)
  {
    output << "Transport baud: " << result.transport_baud_rate << "\n";
  }
  if (result.plan.rate_hz.has_value())
  {
    output << "Rate override: " << FormatCompactDouble(*result.plan.rate_hz) << " Hz\n";
  }
  output << "Command timeout: " << result.timeout_ms << " ms\n\n";

  AppendCommandSequenceText(output, result);

  if (!result.progress_log.empty())
  {
    output << "\nProgress:\n";
    for (const auto& line : result.progress_log)
    {
      output << "  " << line << "\n";
    }
  }

  output << "\nSummary:\n";
  output << "  commands_total: " << result.execution_summary.commands_total << "\n";
  output << "  commands_completed: " << result.execution_summary.commands_completed << "\n";
  output << "  commands_failed: " << result.execution_summary.commands_failed << "\n";
  output << "  commands_retried: " << result.execution_summary.commands_retried << "\n";
  output << "  responses_applied: " << result.execution_summary.responses_applied << "\n";
  output << "  final_status: " << result.execution_summary.final_status << "\n";

  if (!result.error_message.empty())
  {
    output << "  error: " << result.error_message << "\n";
  }

  return output.str();
}

std::string FormatConfigApplyJson(const ConfigApplyResult& result)
{
  std::ostringstream output;
  output << "{\n";
  output << "  \"status\": \"" << ToString(result.status) << "\",\n";
  output << "  \"dry_run\": " << (result.dry_run ? "true" : "false") << ",\n";
  output << "  \"execute_requested\": "
         << (result.execute_requested ? "true" : "false") << ",\n";
  output << "  \"executed\": " << (result.executed ? "true" : "false") << ",\n";
  output << "  \"profile\": {\n";
  output << "    \"vendor\": \"" << EscapeJson(result.plan.vendor) << "\",\n";
  output << "    \"receiver_family\": \"" << EscapeJson(result.plan.receiver_family) << "\",\n";
  output << "    \"name\": \"" << EscapeJson(result.plan.profile) << "\",\n";
  output << "    \"persistent\": " << (result.plan.persistent ? "true" : "false") << ",\n";
  output << "    \"rate_hz\": ";
  if (result.plan.rate_hz.has_value())
  {
    output << FormatCompactDouble(*result.plan.rate_hz, 6);
  }
  else
  {
    output << "null";
  }
  output << "\n";
  output << "  },\n";
  output << "  \"transport\": {\n";
  output << "    \"port\": \"" << EscapeJson(result.port) << "\",\n";
  output << "    \"baud\": " << result.transport_baud_rate << ",\n";
  output << "    \"timeout_ms\": " << result.timeout_ms << "\n";
  output << "  },\n";
  output << "  \"safety\": {\n";
  output << "    \"runtime_confirmation_required\": "
         << (result.requires_runtime_confirmation ? "true" : "false") << ",\n";
  output << "    \"persistent_confirmation_required\": "
         << (result.requires_persistent_confirmation ? "true" : "false") << ",\n";
  output << "    \"execution_confirmed\": "
         << (result.execution_confirmed ? "true" : "false") << "\n";
  output << "  },\n";
  output << "  \"plan_summary\": {\n";
  output << "    \"commands\": " << result.plan.summary.commands_total << ",\n";
  output << "    \"runtime\": " << result.plan.summary.runtime_commands << ",\n";
  output << "    \"persistent\": " << result.plan.summary.persistent_commands << ",\n";
  output << "    \"factory_reset\": " << result.plan.summary.factory_reset_commands << "\n";
  output << "  },\n";
  AppendCommandSequenceJson(output, result);
  output << "  \"progress\": [\n";
  for (std::size_t index = 0; index < result.progress_log.size(); ++index)
  {
    output << "    \"" << EscapeJson(result.progress_log[index]) << "\"";
    if (index + 1u != result.progress_log.size())
    {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"execution_summary\": {\n";
  output << "    \"commands_total\": " << result.execution_summary.commands_total << ",\n";
  output << "    \"commands_completed\": " << result.execution_summary.commands_completed << ",\n";
  output << "    \"commands_failed\": " << result.execution_summary.commands_failed << ",\n";
  output << "    \"commands_retried\": " << result.execution_summary.commands_retried << ",\n";
  output << "    \"responses_applied\": " << result.execution_summary.responses_applied << ",\n";
  output << "    \"final_status\": \""
         << EscapeJson(result.execution_summary.final_status) << "\"\n";
  output << "  },\n";
  output << "  \"error_message\": \"" << EscapeJson(result.error_message) << "\"\n";
  output << "}\n";
  return output.str();
}

}  // namespace universal_gnss_tools
