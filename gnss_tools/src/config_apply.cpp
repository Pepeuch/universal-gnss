#include "universal_gnss_tools/config_apply.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
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
using universal_gnss_driver::ReceiverAutoConfigApplyMode;
using universal_gnss_driver::ReceiverAutoConfigRequest;
using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandPayloadKind;
using universal_gnss_driver::ReceiverCommandResponse;
using universal_gnss_driver::ReceiverCommandResponseKind;
using universal_gnss_driver::ReceiverCommandResponseMatchMetadata;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
using universal_gnss_driver::ReceiverCommandTimestampNs;
using universal_gnss_driver::ReceiverConfigApplication;
using universal_gnss_driver::ReceiverConfigApplicationConfig;
using universal_gnss_driver::ReceiverConfigApplicationResult;
using universal_gnss_driver::ReceiverConfigApplicationState;
using universal_gnss_driver::ReceiverDetectedFamily;
using universal_gnss_driver::ReceiverProbeConfidence;
using universal_gnss_driver::ReceiverProbeResult;
using universal_gnss_driver::UbloxResponseRouter;
using universal_gnss_driver::UbloxRoutedResponse;
using universal_gnss_driver::UnicoreResponseRouter;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::ProtocolTimestampNs;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_transport::ByteDuplex;
using universal_gnss_transport::TransportStatus;

using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kUnicoreFactoryResetRecoveryWindowMs = 60000u;
constexpr std::uint32_t kUnicoreBaudSwitchRecoveryWindowMs = 10000u;
constexpr std::uint32_t kProbeAttemptReadTimeoutMs = 250u;
constexpr std::uint32_t kUnicoreActiveProbeAttemptWindowMs = 2000u;
constexpr std::uint32_t kUnicoreRuntimeBaudSwitchProbeWindowMs = 750u;
constexpr std::uint32_t kUnicoreRuntimeBaudSwitchMaxAttempts = 3u;
constexpr auto kRecoveryProbeRetrySleep = std::chrono::milliseconds(500);
constexpr auto kUnicoreRecoveryQueryRetry = std::chrono::milliseconds(1000);
constexpr std::string_view kUnicoreRecoveryQuery = "VERSIONA\r\n";

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
          stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
                 << std::dec << std::setfill(' ');
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

bool StartsWith(const std::string_view text, const std::string_view prefix)
{
  return text.size() >= prefix.size() && text.compare(0u, prefix.size(), prefix) == 0;
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
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
      .count();
}

bool ApplyModeRequestsExecution(const ReceiverAutoConfigApplyMode apply_mode)
{
  return apply_mode != ReceiverAutoConfigApplyMode::kDryRun;
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
    const auto baud = std::stoul(std::string(remainder.substr(0u, separator)), &parsed, 10);
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

bool PlanHasFactoryResetCommand(const std::vector<ConfigPlanCommand>& commands)
{
  for (const auto& command : commands)
  {
    if (command.command.kind == universal_gnss_driver::ReceiverCommandKind::kReset)
    {
      return true;
    }
  }

  return false;
}

std::string ResolveRequestedDevicePath(const ConfigApplyOptions& options)
{
  if (!options.device_path.empty())
  {
    return options.device_path;
  }

  if (options.discovery_result.has_value())
  {
    return options.discovery_result->path;
  }

  return {};
}

std::uint32_t ResolveRequestedTransportBaud(const ConfigApplyOptions& options)
{
  if (options.transport_baud_rate != 0u)
  {
    return options.transport_baud_rate;
  }

  if (options.discovery_result.has_value() && options.discovery_result->selected_baud.has_value())
  {
    return *options.discovery_result->selected_baud;
  }

  return 0u;
}

ConfigApplyResult MakeBaseResult(const ConfigApplyOptions& options)
{
  ConfigApplyResult result;
  result.execute_requested = ApplyModeRequestsExecution(options.apply_mode);
  result.dry_run = true;
  result.executed = false;
  result.device_path = ResolveRequestedDevicePath(options);
  result.transport_baud_rate = ResolveRequestedTransportBaud(options);
  result.timeout_ms = options.timeout_ms;
  return result;
}

void PopulateExecutionSummaryFromPlan(ConfigApplyResult& result)
{
  result.execution_summary.commands_total = result.plan.summary.commands_total;
}

bool IsUnicorePlan(const ConfigPlanResult& plan)
{
  return plan.vendor == "unicore";
}

bool PlanUsesUnicoreRecoveryWorkflow(const ConfigPlanResult& plan)
{
  return IsUnicorePlan(plan) && plan.summary.factory_reset_commands > 0u;
}

bool PlanUsesUnicoreRuntimeBaudSwitchWorkflow(const ConfigPlanResult& plan,
                                              const std::uint32_t transport_baud_rate)
{
  if (!IsUnicorePlan(plan) || plan.summary.factory_reset_commands > 0u || transport_baud_rate == 0u)
  {
    return false;
  }

  const auto target_baud = ExtractPlannedUnicoreConfigBaud(plan.commands);
  return target_baud.has_value() && *target_baud != 0u && *target_baud != transport_baud_rate;
}

bool ApplyExecutionSafetyRules(ConfigApplyResult& result, const ConfigApplyOptions& options)
{
  const bool execution_requested = ApplyModeRequestsExecution(options.apply_mode);
  const bool has_planned_commands = result.plan.summary.commands_total > 0u;
  const bool uses_unicore_recovery_workflow = PlanUsesUnicoreRecoveryWorkflow(result.plan);
  result.requires_runtime_confirmation =
      execution_requested && result.plan.summary.runtime_commands > 0u;
  result.requires_persistent_confirmation =
      execution_requested && (result.plan.summary.persistent_commands > 0u ||
                              result.plan.summary.factory_reset_commands > 0u);
  result.execution_confirmed = !execution_requested || !has_planned_commands || options.confirm;

  if (result.plan.summary.factory_reset_commands > 0u && !uses_unicore_recovery_workflow)
  {
    result.status = ConfigApplyStatus::kSafetyRejected;
    result.error_message =
        "factory-reset live apply remains guarded until reconnect/probe handling is implemented";
    result.execution_summary.final_status = "safety_rejected";
    return false;
  }

  if (!execution_requested)
  {
    result.execution_summary.final_status = "dry_run";
    return true;
  }

  if (!result.plan.ready_to_execute)
  {
    result.status = ConfigApplyStatus::kSafetyRejected;
    result.error_message =
        "live apply is blocked for this plan because it is not marked ready to execute";
    result.execution_summary.final_status = "safety_rejected";
    return false;
  }

  if (!result.execution_confirmed)
  {
    result.status = ConfigApplyStatus::kSafetyRejected;
    result.error_message =
        "live receiver writes require explicit operator confirmation via --confirm or --yes";
    result.execution_summary.final_status = "safety_rejected";
    return false;
  }

  if (options.apply_mode == ReceiverAutoConfigApplyMode::kPersistent)
  {
    const bool is_ublox_persistent_plan = result.plan.vendor == "ublox";

    if (!uses_unicore_recovery_workflow && !is_ublox_persistent_plan)
    {
      result.status = ConfigApplyStatus::kSafetyRejected;
      result.error_message = "persistent live apply remains guarded for this receiver family";
      result.execution_summary.final_status = "safety_rejected";
      return false;
    }
  }

  return true;
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
      command.explicit_safety_confirmation = options.confirm;
    }

    commands.push_back(std::move(command));
  }

  return commands;
}

std::vector<ReceiverCommand> BuildExecutableCommands(
    const std::vector<ConfigPlanCommand>& plan_commands, const ConfigApplyOptions& options)
{
  std::vector<ReceiverCommand> commands;
  commands.reserve(plan_commands.size());

  for (const auto& plan_command : plan_commands)
  {
    ReceiverCommand command = plan_command.command;
    command.retry_policy.timeout_ms = options.timeout_ms;

    if (command.safety_level == ReceiverCommandSafetyLevel::kPersistent ||
        command.safety_level == ReceiverCommandSafetyLevel::kFactoryReset)
    {
      command.explicit_safety_confirmation = options.confirm;
    }

    commands.push_back(std::move(command));
  }

  return commands;
}

std::uint32_t ResolveTransportReadTimeoutMs(const ConfigApplyOptions& options)
{
  return options.timeout_ms > 100u ? 100u : (options.timeout_ms == 0u ? 1u : options.timeout_ms);
}

std::optional<std::size_t> FindFirstFactoryResetCommandIndex(
    const std::vector<ConfigPlanCommand>& commands)
{
  for (std::size_t index = 0u; index < commands.size(); ++index)
  {
    if (commands[index].command.safety_level == ReceiverCommandSafetyLevel::kFactoryReset ||
        commands[index].command.kind == universal_gnss_driver::ReceiverCommandKind::kReset)
    {
      return index;
    }
  }

  return std::nullopt;
}

bool IsUnicoreCom1BaudCommand(const ConfigPlanCommand& command)
{
  return command.command.payload.kind == ReceiverCommandPayloadKind::kText &&
         TrimTrailingCrLf(command.command.payload.text).rfind("CONFIG COM1 ", 0u) == 0u;
}

std::optional<std::size_t> FindFirstUnicoreBaudCommandIndex(
    const std::vector<ConfigPlanCommand>& commands, const std::size_t begin_index)
{
  for (std::size_t index = begin_index; index < commands.size(); ++index)
  {
    if (IsUnicoreCom1BaudCommand(commands[index]))
    {
      return index;
    }
  }

  return std::nullopt;
}

std::vector<ConfigPlanCommand> SlicePlanCommands(const std::vector<ConfigPlanCommand>& commands,
                                                 const std::size_t begin_index,
                                                 const std::size_t end_index)
{
  if (begin_index >= end_index || begin_index >= commands.size())
  {
    return {};
  }

  const auto bounded_end = std::min(end_index, commands.size());
  return std::vector<ConfigPlanCommand>(commands.begin() + static_cast<std::ptrdiff_t>(begin_index),
                                        commands.begin() +
                                            static_cast<std::ptrdiff_t>(bounded_end));
}

std::uint32_t ResolveUnicoreRecoveryBaud(const ConfigApplyResult& result)
{
  if (const auto planned_baud = ExtractPlannedUnicoreConfigBaud(result.plan.commands);
      planned_baud.has_value() && *planned_baud != 0u)
  {
    return *planned_baud;
  }

  if (result.plan.baud.has_value() && *result.plan.baud != 0u)
  {
    return *result.plan.baud;
  }

  if (result.transport_baud_rate != 0u)
  {
    return result.transport_baud_rate;
  }

  if (result.plan.detected_baud.has_value() && *result.plan.detected_baud != 0u)
  {
    return *result.plan.detected_baud;
  }

  return 921600u;
}

std::string DescribeTransportFailure(const TransportStatus status,
                                     const universal_gnss_transport::TransportError error)
{
  std::ostringstream stream;
  stream << "transport status=" << static_cast<int>(status) << " error=" << static_cast<int>(error);
  return stream.str();
}

enum class UnicoreActiveProbeStatus : std::uint8_t
{
  kResponsive = 0,
  kTimedOut = 1,
  kTransportError = 2,
  kRejected = 3,
};

struct UnicoreActiveProbeOutcome
{
  UnicoreActiveProbeStatus status{UnicoreActiveProbeStatus::kTimedOut};
  std::string error_message{};
};

std::uint32_t ComputeProbeReadAttemptLimit(const std::uint32_t window_ms)
{
  if (window_ms == 0u)
  {
    return 1u;
  }

  const auto sleep_ms = static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(kRecoveryProbeRetrySleep).count());
  if (sleep_ms == 0u)
  {
    return 1u;
  }

  return std::max<std::uint32_t>(1u, (window_ms + sleep_ms - 1u) / sleep_ms);
}

UnicoreActiveProbeOutcome ProbeUnicoreActiveResponse(ByteDuplex& transport,
                                                     const std::uint32_t baud_rate,
                                                     const std::uint32_t window_ms)
{
  UnicoreActiveProbeOutcome outcome;

  UnicoreResponseRouter router;
  std::vector<std::uint8_t> read_buffer(256u, 0u);
  const auto deadline = Clock::now() + std::chrono::milliseconds(static_cast<int>(window_ms));
  auto next_query_time = Clock::now();
  const auto max_read_attempts = ComputeProbeReadAttemptLimit(window_ms);
  std::uint32_t read_attempt = 0u;

  while (Clock::now() < deadline && read_attempt < max_read_attempts)
  {
    const auto now = Clock::now();
    if (now >= next_query_time)
    {
      const auto write_result =
          transport.Write(reinterpret_cast<const std::uint8_t*>(kUnicoreRecoveryQuery.data()),
                          kUnicoreRecoveryQuery.size());
      if (write_result.status == TransportStatus::kClosed ||
          write_result.status == TransportStatus::kError ||
          write_result.bytes_written != kUnicoreRecoveryQuery.size())
      {
        outcome.status = UnicoreActiveProbeStatus::kTransportError;
        outcome.error_message =
            "failed to send active Unicore recovery query at " + std::to_string(baud_rate) +
            " bps: " + DescribeTransportFailure(write_result.status, write_result.error);
        return outcome;
      }

      next_query_time = now + kUnicoreRecoveryQueryRetry;
    }

    const auto read_result = transport.Read(read_buffer.data(), read_buffer.size());
    if (read_result.status == TransportStatus::kClosed ||
        read_result.status == TransportStatus::kError)
    {
      outcome.status = UnicoreActiveProbeStatus::kTransportError;
      outcome.error_message = "failed while waiting for an active Unicore recovery response at " +
                              std::to_string(baud_rate) + " bps: " +
                              DescribeTransportFailure(read_result.status, read_result.error);
      return outcome;
    }

    if (read_result.bytes_read == 0u)
    {
      ++read_attempt;
      std::this_thread::sleep_for(kRecoveryProbeRetrySleep);
      continue;
    }

    router.FeedBytes(std::string_view(reinterpret_cast<const char*>(read_buffer.data()),
                                      read_result.bytes_read),
                     NowTimestampNs());

    ReceiverCommandResponse response;
    while (router.PopResponse(response))
    {
      if (response.kind == ReceiverCommandResponseKind::kTextOk)
      {
        outcome.status = UnicoreActiveProbeStatus::kResponsive;
        return outcome;
      }

      if (response.kind == ReceiverCommandResponseKind::kTextError)
      {
        outcome.status = UnicoreActiveProbeStatus::kRejected;
        outcome.error_message =
            "receiver returned an explicit error while handling the active Unicore recovery "
            "query: " +
            response.message;
        return outcome;
      }
    }

    ++read_attempt;
  }

  outcome.status = UnicoreActiveProbeStatus::kTimedOut;
  outcome.error_message = "receiver reopened at " + std::to_string(baud_rate) +
                          " bps but did not answer VERSIONA before the recovery timeout";
  return outcome;
}

bool ReopenTransportUntilReady(ConfigApplyTransportHooks& hooks,
                               ByteDuplex& transport,
                               ConfigApplyResult& result,
                               const std::string& device_path,
                               const std::uint32_t baud_rate,
                               const std::uint32_t read_timeout_ms,
                               const std::uint32_t window_ms,
                               const std::string& waiting_message,
                               std::string& error_message)
{
  result.progress_log.push_back(waiting_message);

  const auto deadline = Clock::now() + std::chrono::milliseconds(static_cast<int>(window_ms));

  do
  {
    if (hooks.ReopenTransport(transport, device_path, baud_rate, read_timeout_ms, error_message))
    {
      return true;
    }

    static_cast<universal_gnss_transport::ByteSource&>(transport).Close();
    std::this_thread::sleep_for(kRecoveryProbeRetrySleep);
  } while (Clock::now() < deadline);

  if (error_message.empty())
  {
    error_message = "receiver transport could not be reopened before the recovery timeout";
  }
  return false;
}

bool WaitForUnicoreActiveResponse(ByteDuplex& transport,
                                  ConfigApplyResult& result,
                                  const std::uint32_t baud_rate,
                                  const std::uint32_t window_ms,
                                  const std::string& waiting_message,
                                  std::string& error_message)
{
  result.progress_log.push_back(waiting_message);

  const auto outcome = ProbeUnicoreActiveResponse(transport, baud_rate, window_ms);
  if (outcome.status == UnicoreActiveProbeStatus::kResponsive)
  {
    result.progress_log.push_back("Active VERSIONA query confirmed Unicore response at " +
                                  std::to_string(baud_rate) + " bps");
    error_message.clear();
    return true;
  }

  error_message = outcome.error_message;
  return false;
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
      application_result.command_index < total ? (application_result.command_index + 1u) : total;

  if (application_result.command_started && application_result.command_index < total)
  {
    const auto& command = plan.commands[application_result.command_index];
    result.progress_log.push_back(MakeCommandProgressPrefix(display_index, total, "dispatching") +
                                  " - " + command.description);
  }

  if (application_result.retry_dispatched && application_result.command_index < total)
  {
    const auto& command = plan.commands[application_result.command_index];
    result.progress_log.push_back(MakeCommandProgressPrefix(display_index, total, "retrying") +
                                  " - " + command.description);
  }

  if (application_result.command_finished)
  {
    std::size_t plan_index = application_result.command_index;
    if (application_result.advanced_to_next_command && application_result.command_index > 0u)
    {
      plan_index = application_result.command_index - 1u;
    }
    else if (application_result.state == ReceiverConfigApplicationState::kCompleted && total > 0u)
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

    const bool succeeded = application_result.state == ReceiverConfigApplicationState::kRunning ||
                           application_result.state == ReceiverConfigApplicationState::kCompleted;

    result.progress_log.push_back(
        MakeCommandProgressPrefix(plan_index + 1u, total, succeeded ? "completed" : "failed") +
        " - " + plan.commands[plan_index].description + suffix);
  }
}

void NoteApplicationProgress(ConfigApplyResult& result,
                             const std::vector<ConfigPlanCommand>& commands,
                             const ReceiverConfigApplicationResult& application_result,
                             const std::size_t command_index_offset,
                             const std::size_t overall_command_total)
{
  const std::size_t total = commands.size();
  if (total == 0u)
  {
    return;
  }

  const std::size_t display_index =
      application_result.command_index < total
          ? (command_index_offset + application_result.command_index + 1u)
          : std::min(overall_command_total, command_index_offset + total);

  if (application_result.command_started && application_result.command_index < total)
  {
    const auto& command = commands[application_result.command_index];
    result.progress_log.push_back(
        MakeCommandProgressPrefix(display_index, overall_command_total, "dispatching") + " - " +
        command.description);
  }

  if (application_result.retry_dispatched && application_result.command_index < total)
  {
    const auto& command = commands[application_result.command_index];
    result.progress_log.push_back(
        MakeCommandProgressPrefix(display_index, overall_command_total, "retrying") + " - " +
        command.description);
  }

  if (application_result.command_finished)
  {
    std::size_t phase_index = application_result.command_index;
    if (application_result.advanced_to_next_command && application_result.command_index > 0u)
    {
      phase_index = application_result.command_index - 1u;
    }
    else if (application_result.state == ReceiverConfigApplicationState::kCompleted && total > 0u)
    {
      phase_index = total - 1u;
    }

    if (phase_index >= total)
    {
      phase_index = total > 0u ? (total - 1u) : 0u;
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

    const bool succeeded = application_result.state == ReceiverConfigApplicationState::kRunning ||
                           application_result.state == ReceiverConfigApplicationState::kCompleted;
    const auto global_index = command_index_offset + phase_index + 1u;

    result.progress_log.push_back(MakeCommandProgressPrefix(global_index,
                                                            overall_command_total,
                                                            succeeded ? "completed" : "failed") +
                                  " - " + commands[phase_index].description + suffix);
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

void UpdateExecutionSummary(ConfigApplyResult& result, const ReceiverConfigApplication& application)
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

struct CommandPhaseOutcome
{
  ConfigApplyStatus status{ConfigApplyStatus::kOk};
  ConfigApplyExecutionSummary summary{};
  std::string error_message{};
};

CommandPhaseOutcome ExecuteUnicoreCommandPhase(ConfigApplyResult& result,
                                               ByteDuplex& transport,
                                               const std::vector<ConfigPlanCommand>& commands,
                                               const ConfigApplyOptions& options,
                                               const std::size_t command_index_offset)
{
  CommandPhaseOutcome outcome;
  outcome.summary.commands_total = commands.size();
  if (commands.empty())
  {
    outcome.summary.final_status = "completed";
    return outcome;
  }

  ReceiverConfigApplicationConfig application_config;
  ReceiverConfigApplication application(transport, application_config);
  UnicoreResponseRouter unicore_router;

  auto executable_commands = BuildExecutableCommands(commands, options);
  auto application_result = application.Start(executable_commands, NowTimestampNs());
  NoteApplicationProgress(result,
                          commands,
                          application_result,
                          command_index_offset,
                          result.plan.summary.commands_total);

  std::vector<std::uint8_t> read_buffer(256u, 0u);
  while (application.state() == ReceiverConfigApplicationState::kRunning ||
         application.state() == ReceiverConfigApplicationState::kWaitingForResponse)
  {
    if (application.state() == ReceiverConfigApplicationState::kRunning)
    {
      application_result = application.Step(NowTimestampNs());
      NoteApplicationProgress(result,
                              commands,
                              application_result,
                              command_index_offset,
                              result.plan.summary.commands_total);
    }

    if (application.state() == ReceiverConfigApplicationState::kCompleted ||
        application.state() == ReceiverConfigApplicationState::kFailed)
    {
      break;
    }

    if (application.state() != ReceiverConfigApplicationState::kWaitingForResponse)
    {
      continue;
    }

    while (application.state() == ReceiverConfigApplicationState::kWaitingForResponse &&
           unicore_router.pending_response_count() > 0u)
    {
      ReceiverCommandResponse response;
      if (!unicore_router.PopResponse(response))
      {
        break;
      }

      application_result = application.ApplyResponse(response);
      NoteApplicationProgress(result,
                              commands,
                              application_result,
                              command_index_offset,
                              result.plan.summary.commands_total);
    }

    if (application.state() == ReceiverConfigApplicationState::kCompleted ||
        application.state() == ReceiverConfigApplicationState::kFailed)
    {
      break;
    }

    const auto read_result = transport.Read(read_buffer.data(), read_buffer.size());
    if (read_result.status == TransportStatus::kError ||
        read_result.status == TransportStatus::kClosed)
    {
      outcome.status = ConfigApplyStatus::kReadFailed;
      outcome.error_message = "transport read failed while waiting for a response";
      result.progress_log.push_back("Read failed while waiting for receiver response");
      break;
    }

    if (read_result.bytes_read > 0u)
    {
      const auto timestamp_ns = NowTimestampNs();
      const std::string_view text(reinterpret_cast<const char*>(read_buffer.data()),
                                  read_result.bytes_read);
      unicore_router.FeedBytes(text, timestamp_ns);

      while (application.state() == ReceiverConfigApplicationState::kWaitingForResponse &&
             unicore_router.pending_response_count() > 0u)
      {
        ReceiverCommandResponse response;
        if (!unicore_router.PopResponse(response))
        {
          break;
        }

        application_result = application.ApplyResponse(response);
        NoteApplicationProgress(result,
                                commands,
                                application_result,
                                command_index_offset,
                                result.plan.summary.commands_total);
      }
    }

    if (application.state() == ReceiverConfigApplicationState::kCompleted ||
        application.state() == ReceiverConfigApplicationState::kFailed)
    {
      break;
    }

    application_result = application.CheckTimeout(NowTimestampNs());
    NoteApplicationProgress(result,
                            commands,
                            application_result,
                            command_index_offset,
                            result.plan.summary.commands_total);
  }

  outcome.summary.commands_completed = application.metrics().commands_completed;
  outcome.summary.commands_failed = application.metrics().commands_failed;
  outcome.summary.commands_retried = application.metrics().commands_retried;
  outcome.summary.responses_applied = application.metrics().responses_applied;

  if (outcome.status == ConfigApplyStatus::kReadFailed)
  {
    outcome.summary.final_status = ToString(outcome.status);
    return outcome;
  }

  if (application.state() == ReceiverConfigApplicationState::kCompleted)
  {
    outcome.summary.final_status = "completed";
    return outcome;
  }

  outcome.status =
      application_result.engine_result.has_value() &&
              application_result.engine_result->status ==
                  universal_gnss_driver::ReceiverCommandTransactionEngineStepStatus::kRejected
          ? ConfigApplyStatus::kRejected
          : MapApplicationFailureStatus(application_result);
  outcome.error_message = !application_result.error_message.empty()
                              ? application_result.error_message
                              : (application_result.engine_result.has_value()
                                     ? application_result.engine_result->error_message
                                     : std::string{"configuration apply failed"});
  outcome.summary.final_status = ToString(outcome.status);
  return outcome;
}

ConfigApplyResult ExecuteUnicoreRecoveryWorkflow(ByteDuplex& transport,
                                                 const ConfigApplyOptions& options,
                                                 ConfigApplyResult result,
                                                 ConfigApplyTransportHooks& hooks)
{
  const auto reset_index = FindFirstFactoryResetCommandIndex(result.plan.commands);
  const auto baud_index =
      reset_index.has_value()
          ? FindFirstUnicoreBaudCommandIndex(result.plan.commands, *reset_index + 1u)
          : std::nullopt;

  if (!reset_index.has_value() || !baud_index.has_value() || *baud_index <= *reset_index)
  {
    result.status = ConfigApplyStatus::kBuildError;
    result.error_message =
        "Unicore recovery workflow could not identify the reset and COM1 baud commands";
    result.execution_summary.final_status = "build_error";
    return result;
  }

  const auto recovery_baud = ResolveUnicoreRecoveryBaud(result);
  const auto transport_read_timeout_ms = ResolveTransportReadTimeoutMs(options);

  const auto reset_phase = SlicePlanCommands(result.plan.commands, 0u, *reset_index + 1u);
  const auto baud_phase =
      SlicePlanCommands(result.plan.commands, *reset_index + 1u, *baud_index + 1u);
  const auto profile_phase =
      SlicePlanCommands(result.plan.commands, *baud_index + 1u, result.plan.commands.size());

  result.dry_run = false;
  result.executed = true;
  result.execution_summary.commands_total = result.plan.summary.commands_total;

  auto phase = ExecuteUnicoreCommandPhase(result, transport, reset_phase, options, 0u);
  result.execution_summary.commands_completed += phase.summary.commands_completed;
  result.execution_summary.commands_failed += phase.summary.commands_failed;
  result.execution_summary.commands_retried += phase.summary.commands_retried;
  result.execution_summary.responses_applied += phase.summary.responses_applied;
  if (phase.status != ConfigApplyStatus::kOk)
  {
    result.status = phase.status;
    result.error_message = phase.error_message;
    result.execution_summary.final_status = phase.summary.final_status;
    return result;
  }

  static_cast<universal_gnss_transport::ByteSource&>(transport).Close();

  std::string transport_error;
  if (!ReopenTransportUntilReady(hooks,
                                 transport,
                                 result,
                                 result.device_path,
                                 115200u,
                                 transport_read_timeout_ms,
                                 kUnicoreFactoryResetRecoveryWindowMs,
                                 "Waiting for Unicore receiver restart after FRESET (up to 45 s)",
                                 transport_error))
  {
    result.status = ConfigApplyStatus::kTransportUnavailable;
    result.error_message = transport_error;
    result.execution_summary.final_status = "transport_unavailable";
    return result;
  }

  result.progress_log.push_back("Reopened transport at 115200 bps after FRESET");
  if (!WaitForUnicoreActiveResponse(
          transport,
          result,
          115200u,
          kUnicoreFactoryResetRecoveryWindowMs,
          "Waiting for an active Unicore response at 115200 bps after FRESET",
          transport_error))
  {
    result.status = ConfigApplyStatus::kTransportUnavailable;
    result.error_message = transport_error;
    result.execution_summary.final_status = "transport_unavailable";
    return result;
  }

  static_cast<universal_gnss_transport::ByteSource&>(transport).Close();
  if (!ReopenTransportUntilReady(hooks,
                                 transport,
                                 result,
                                 result.device_path,
                                 115200u,
                                 transport_read_timeout_ms,
                                 kUnicoreBaudSwitchRecoveryWindowMs,
                                 "Reopening transport at 115200 bps for COM1 recovery",
                                 transport_error))
  {
    result.status = ConfigApplyStatus::kTransportUnavailable;
    result.error_message = transport_error;
    result.execution_summary.final_status = "transport_unavailable";
    return result;
  }

  result.progress_log.push_back("Reopened transport at 115200 bps for COM1 recovery");
  phase = ExecuteUnicoreCommandPhase(result, transport, baud_phase, options, reset_phase.size());
  result.execution_summary.commands_completed += phase.summary.commands_completed;
  result.execution_summary.commands_failed += phase.summary.commands_failed;
  result.execution_summary.commands_retried += phase.summary.commands_retried;
  result.execution_summary.responses_applied += phase.summary.responses_applied;
  if (phase.status != ConfigApplyStatus::kOk)
  {
    result.status = phase.status;
    result.error_message = phase.error_message;
    result.execution_summary.final_status = phase.summary.final_status;
    return result;
  }

  static_cast<universal_gnss_transport::ByteSource&>(transport).Close();
  if (!ReopenTransportUntilReady(hooks,
                                 transport,
                                 result,
                                 result.device_path,
                                 recovery_baud,
                                 transport_read_timeout_ms,
                                 kUnicoreBaudSwitchRecoveryWindowMs,
                                 "Waiting for Unicore receiver to reopen at " +
                                     std::to_string(recovery_baud) +
                                     " bps after COM1 reconfiguration",
                                 transport_error))
  {
    result.status = ConfigApplyStatus::kTransportUnavailable;
    result.error_message = transport_error;
    result.execution_summary.final_status = "transport_unavailable";
    return result;
  }

  result.progress_log.push_back("Reopened transport at " + std::to_string(recovery_baud) +
                                " bps after COM1 recovery");
  if (!WaitForUnicoreActiveResponse(transport,
                                    result,
                                    recovery_baud,
                                    kUnicoreBaudSwitchRecoveryWindowMs,
                                    "Waiting for an active Unicore response at " +
                                        std::to_string(recovery_baud) + " bps after COM1 recovery",
                                    transport_error))
  {
    result.status = ConfigApplyStatus::kTransportUnavailable;
    result.error_message = transport_error;
    result.execution_summary.final_status = "transport_unavailable";
    return result;
  }

  static_cast<universal_gnss_transport::ByteSource&>(transport).Close();
  if (!ReopenTransportUntilReady(hooks,
                                 transport,
                                 result,
                                 result.device_path,
                                 recovery_baud,
                                 transport_read_timeout_ms,
                                 kUnicoreBaudSwitchRecoveryWindowMs,
                                 "Reopening transport at " + std::to_string(recovery_baud) +
                                     " bps for post-reset profile apply",
                                 transport_error))
  {
    result.status = ConfigApplyStatus::kTransportUnavailable;
    result.error_message = transport_error;
    result.execution_summary.final_status = "transport_unavailable";
    return result;
  }

  result.progress_log.push_back("Reopened transport at " + std::to_string(recovery_baud) +
                                " bps for post-reset profile apply");
  phase = ExecuteUnicoreCommandPhase(
      result, transport, profile_phase, options, reset_phase.size() + baud_phase.size());
  result.execution_summary.commands_completed += phase.summary.commands_completed;
  result.execution_summary.commands_failed += phase.summary.commands_failed;
  result.execution_summary.commands_retried += phase.summary.commands_retried;
  result.execution_summary.responses_applied += phase.summary.responses_applied;
  if (phase.status != ConfigApplyStatus::kOk)
  {
    result.status = phase.status;
    result.error_message = phase.error_message;
    result.execution_summary.final_status = phase.summary.final_status;
    return result;
  }

  result.transport_baud_rate = recovery_baud;
  result.status = ConfigApplyStatus::kOk;
  result.execution_summary.final_status = "completed";
  result.progress_log.push_back("Verified receiver is reachable again at " +
                                std::to_string(recovery_baud) +
                                " bps after post-reset profile apply");
  result.error_message.clear();
  return result;
}

ConfigApplyResult ExecuteUnicoreRuntimeBaudSwitchWorkflow(ByteDuplex& transport,
                                                          const ConfigApplyOptions& options,
                                                          ConfigApplyResult result,
                                                          ConfigApplyTransportHooks& hooks)
{
  const auto baud_index = FindFirstUnicoreBaudCommandIndex(result.plan.commands, 0u);
  const auto target_baud = ExtractPlannedUnicoreConfigBaud(result.plan.commands);

  if (!baud_index.has_value() || !target_baud.has_value() || *target_baud == 0u ||
      result.transport_baud_rate == 0u)
  {
    result.status = ConfigApplyStatus::kBuildError;
    result.error_message =
        "Unicore runtime baud-switch workflow could not identify the live and target baud values";
    result.execution_summary.final_status = "build_error";
    return result;
  }

  const auto current_baud = result.transport_baud_rate;
  const auto transport_read_timeout_ms = ResolveTransportReadTimeoutMs(options);
  const auto baud_phase = SlicePlanCommands(result.plan.commands, 0u, *baud_index + 1u);
  const auto profile_phase =
      SlicePlanCommands(result.plan.commands, *baud_index + 1u, result.plan.commands.size());

  result.dry_run = false;
  result.executed = true;
  result.execution_summary.commands_total = result.plan.summary.commands_total;

  auto phase = ExecuteUnicoreCommandPhase(result, transport, baud_phase, options, 0u);
  result.execution_summary.commands_completed += phase.summary.commands_completed;
  result.execution_summary.commands_failed += phase.summary.commands_failed;
  result.execution_summary.commands_retried += phase.summary.commands_retried;
  result.execution_summary.responses_applied += phase.summary.responses_applied;
  if (phase.status != ConfigApplyStatus::kOk)
  {
    result.status = phase.status;
    result.error_message = phase.error_message;
    result.execution_summary.final_status = phase.summary.final_status;
    return result;
  }

  std::uint32_t active_baud = current_baud;
  bool continue_at_target = false;
  bool continue_at_old = false;
  std::string last_error = "no receiver response on probed baud rates after CONFIG COM1: old " +
                           std::to_string(current_baud) + " bps, target " +
                           std::to_string(*target_baud) + " bps";
  result.progress_log.push_back("Verifying which Unicore baud is live after CONFIG COM1: old " +
                                std::to_string(current_baud) + " bps, target " +
                                std::to_string(*target_baud) + " bps");

  for (std::uint32_t attempt = 1u; attempt <= kUnicoreRuntimeBaudSwitchMaxAttempts; ++attempt)
  {
    result.progress_log.push_back("Post-CONFIG COM1 probe attempt " + std::to_string(attempt) +
                                  "/" + std::to_string(kUnicoreRuntimeBaudSwitchMaxAttempts));

    static_cast<universal_gnss_transport::ByteSource&>(transport).Close();
    std::string reopen_error;
    if (!hooks.ReopenTransport(
            transport, result.device_path, current_baud, transport_read_timeout_ms, reopen_error))
    {
      last_error = reopen_error;
    }
    bool old_baud_responsive = false;
    if (static_cast<universal_gnss_transport::ByteSource&>(transport).IsOpen())
    {
      const auto old_probe = ProbeUnicoreActiveResponse(transport,
                                                        current_baud,
                                                        kUnicoreRuntimeBaudSwitchProbeWindowMs);
      if (old_probe.status == UnicoreActiveProbeStatus::kResponsive)
      {
        old_baud_responsive = true;
      }
      else
      {
        last_error = old_probe.error_message;
      }
    }

    static_cast<universal_gnss_transport::ByteSource&>(transport).Close();
    std::string target_reopen_error;
    if (hooks.ReopenTransport(transport,
                              result.device_path,
                              *target_baud,
                              transport_read_timeout_ms,
                              target_reopen_error))
    {
      const auto target_probe = ProbeUnicoreActiveResponse(transport,
                                                           *target_baud,
                                                           kUnicoreRuntimeBaudSwitchProbeWindowMs);
      if (target_probe.status == UnicoreActiveProbeStatus::kResponsive)
      {
        continue_at_target = true;
        active_baud = *target_baud;
        result.progress_log.push_back("Target configured baud " + std::to_string(*target_baud) +
                                      " bps answered VERSIONA after CONFIG COM1");
        break;
      }
      last_error = target_probe.error_message;
    }
    else if (!target_reopen_error.empty())
    {
      last_error = target_reopen_error;
    }

    if (old_baud_responsive)
    {
      static_cast<universal_gnss_transport::ByteSource&>(transport).Close();
      std::string old_reopen_error;
      if (hooks.ReopenTransport(transport,
                                result.device_path,
                                current_baud,
                                transport_read_timeout_ms,
                                old_reopen_error))
      {
        active_baud = current_baud;
        continue_at_old = true;
        result.progress_log.push_back(
            "Configured baud " + std::to_string(*target_baud) +
            " bps did not become active live; continuing at the previously detected " +
            std::to_string(current_baud) + " bps transport");
        result.plan.warnings.push_back(
            "configured baud " + std::to_string(*target_baud) +
            " bps did not become active live after CONFIG COM1; continuing at the previously "
            "detected " +
            std::to_string(current_baud) +
            " bps transport until a persistent/save workflow or reboot makes the new baud active");
        break;
      }
      last_error = old_reopen_error;
    }

    if (attempt < kUnicoreRuntimeBaudSwitchMaxAttempts)
    {
      std::this_thread::sleep_for(kRecoveryProbeRetrySleep);
    }
  }

  if (!continue_at_target && !continue_at_old)
  {
    result.status = ConfigApplyStatus::kTransportUnavailable;
    result.error_message = "no receiver response on probed baud rates after CONFIG COM1: old " +
                           std::to_string(current_baud) + " bps, target " +
                           std::to_string(*target_baud) + " bps";
    result.execution_summary.final_status = "transport_unavailable";
    return result;
  }

  if (continue_at_target)
  {
    static_cast<universal_gnss_transport::ByteSource&>(transport).Close();
    std::string target_reopen_error;
    if (!hooks.ReopenTransport(transport,
                               result.device_path,
                               active_baud,
                               transport_read_timeout_ms,
                               target_reopen_error))
    {
      result.status = ConfigApplyStatus::kTransportUnavailable;
      result.error_message = target_reopen_error.empty()
                                 ? "failed to reopen the verified target baud transport"
                                 : target_reopen_error;
      result.execution_summary.final_status = "transport_unavailable";
      return result;
    }
  }

  phase = ExecuteUnicoreCommandPhase(result, transport, profile_phase, options, baud_phase.size());
  result.execution_summary.commands_completed += phase.summary.commands_completed;
  result.execution_summary.commands_failed += phase.summary.commands_failed;
  result.execution_summary.commands_retried += phase.summary.commands_retried;
  result.execution_summary.responses_applied += phase.summary.responses_applied;
  if (phase.status != ConfigApplyStatus::kOk)
  {
    result.status = phase.status;
    result.error_message = phase.error_message;
    result.execution_summary.final_status = phase.summary.final_status;
    return result;
  }

  result.transport_baud_rate = active_baud;
  result.status = ConfigApplyStatus::kOk;
  result.execution_summary.final_status = "completed";
  if (continue_at_target)
  {
    result.progress_log.push_back("Continuing runtime-only profile apply at target baud " +
                                  std::to_string(active_baud) + " bps");
  }
  else
  {
    result.progress_log.push_back("Continuing runtime-only profile apply at previous live baud " +
                                  std::to_string(active_baud) + " bps");
  }
  result.error_message.clear();
  return result;
}

ReceiverAutoConfigRequest BuildAutoConfigRequest(const ConfigApplyOptions& options)
{
  ReceiverAutoConfigRequest request;
  request.receiver_family = options.receiver_family;
  request.discovery_result = options.discovery_result;
  request.requested_profile = options.profile;
  request.apply_mode = options.apply_mode;
  request.receiver_model = options.receiver_model;
  request.signal_profile = options.signal_profile;
  request.signal_group_override = options.signal_group_override;
  request.output_port = options.output_port;
  request.config_baud = options.config_baud;
  request.rate_hz = options.rate_hz;
  if (!options.device_path.empty())
  {
    request.transport_device_path = options.device_path;
  }
  return request;
}

void AppendCommandSequenceText(std::ostringstream& output, const ConfigApplyResult& result)
{
  output << "Command sequence:\n";
  for (std::size_t index = 0; index < result.plan.commands.size(); ++index)
  {
    const auto& command = result.plan.commands[index];
    output << '\n'
           << (index + 1u) << ". " << CommandKindToString(command.command.kind) << " ["
           << SafetyLevelToString(command.command.safety_level);
    if (command.requires_explicit_safety_confirmation)
    {
      output << ", dispatcher_confirmation_required";
    }
    output << "]\n";
    output << "   payload: " << PayloadKindToString(command.command.payload.kind) << ", "
           << command.payload_bytes << " bytes\n";
    output << "   description: " << command.description << "\n";

    if (command.command.payload.kind == ReceiverCommandPayloadKind::kText)
    {
      output << "   command: " << TrimTrailingCrLf(command.command.payload.text) << "\n";
    }
  }
}

void AppendCommandSequenceJson(std::ostringstream& output, const ConfigApplyResult& result)
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
    output << "      \"payload_kind\": \"" << PayloadKindToString(command.command.payload.kind)
           << "\",\n";
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

  if (options.timeout_ms == 0u)
  {
    result.status = ConfigApplyStatus::kInvalidArgument;
    result.error_message = "timeout-ms must be non-zero";
    result.execution_summary.final_status = ToString(result.status);
    return result;
  }

  result.plan = BuildConfigPlan(BuildAutoConfigRequest(options));
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
                                     const ConfigApplyOptions& options,
                                     ConfigApplyTransportHooks* hooks)
{
  ConfigApplyResult result = PrepareConfigApply(options);
  if (result.status != ConfigApplyStatus::kOk || !ApplyModeRequestsExecution(options.apply_mode))
  {
    return result;
  }

  if (result.plan.commands.empty())
  {
    result.dry_run = false;
    result.executed = true;
    result.execution_summary.final_status = "completed";
    result.progress_log.push_back(
        "No receiver configuration commands were required for this profile");
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

  if (PlanUsesUnicoreRecoveryWorkflow(result.plan))
  {
    if (hooks == nullptr)
    {
      result.status = ConfigApplyStatus::kTransportUnavailable;
      result.error_message =
          "Unicore reset/recovery apply requires transport hooks for reprobe and reopen";
      result.execution_summary.final_status = "transport_unavailable";
      return result;
    }

    return ExecuteUnicoreRecoveryWorkflow(transport, options, std::move(result), *hooks);
  }

  if (PlanUsesUnicoreRuntimeBaudSwitchWorkflow(result.plan, result.transport_baud_rate))
  {
    if (hooks == nullptr)
    {
      result.status = ConfigApplyStatus::kTransportUnavailable;
      result.error_message =
          "Unicore runtime baud-switch apply requires transport hooks for reopen/probe handling";
      result.execution_summary.final_status = "transport_unavailable";
      return result;
    }

    return ExecuteUnicoreRuntimeBaudSwitchWorkflow(transport, options, std::move(result), *hooks);
  }

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
      const bool stopped = result.plan.vendor == "ublox"
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
  output << "Status: " << ToString(result.status) << "\n";
  output << "Receiver family: " << result.plan.receiver_family << "\n";
  if (result.plan.receiver_model.has_value())
  {
    output << "Receiver model: " << *result.plan.receiver_model << "\n";
  }
  output << "Profile: " << result.plan.vendor << ' ' << result.plan.profile << "\n";
  output << "Apply mode: " << result.plan.apply_mode << "\n";
  output << "Dry run: " << (result.dry_run ? "yes" : "no") << "\n";
  output << "Live apply requested: " << (result.execute_requested ? "yes" : "no") << "\n";
  output << "Executed: " << (result.executed ? "yes" : "no") << "\n";
  output << "Production ready: " << (result.plan.production_ready ? "yes" : "no") << "\n";
  output << "Ready to execute: " << (result.plan.ready_to_execute ? "yes" : "no") << "\n";
  output << "Runtime confirmation required: "
         << (result.requires_runtime_confirmation ? "yes" : "no") << "\n";
  output << "Persistent confirmation required: "
         << (result.requires_persistent_confirmation ? "yes" : "no") << "\n";
  output << "Execution confirmed: " << (result.execution_confirmed ? "yes" : "no") << "\n";
  output << "Command count: " << result.plan.summary.commands_total << "\n";
  output << "Runtime commands: " << result.plan.summary.runtime_commands << "\n";
  output << "Persistent commands: " << result.plan.summary.persistent_commands << "\n";
  output << "Factory-reset commands: " << result.plan.summary.factory_reset_commands << "\n";
  if (result.plan.detected_device.has_value())
  {
    output << "Detected device: " << *result.plan.detected_device << "\n";
  }
  if (result.plan.detected_stable_id.has_value())
  {
    output << "Detected stable id: " << *result.plan.detected_stable_id << "\n";
  }
  if (result.plan.detected_baud.has_value())
  {
    output << "Detected receiver baud: " << *result.plan.detected_baud << "\n";
  }
  if (result.plan.discovery_confidence.has_value())
  {
    output << "Discovery confidence: " << *result.plan.discovery_confidence << "\n";
  }
  if (result.plan.discovery_score.has_value())
  {
    output << "Discovery score: " << *result.plan.discovery_score << "\n";
  }
  if (!result.device_path.empty())
  {
    output << "Device: " << result.device_path << "\n";
  }
  if (result.transport_baud_rate != 0u)
  {
    output << "Current transport baud: " << result.transport_baud_rate << "\n";
  }
  if (PlanHasFactoryResetCommand(result.plan.commands))
  {
    output << "Factory reset baud: 115200\n";
  }
  if (const auto target_baud = ExtractPlannedUnicoreConfigBaud(result.plan.commands);
      target_baud.has_value())
  {
    output << "Target configured baud: " << *target_baud << "\n";
  }
  if (result.plan.baud.has_value())
  {
    output << "Config baud override: " << *result.plan.baud << "\n";
  }
  if (result.plan.signal_profile.has_value())
  {
    output << "Signal profile override: "
           << universal_gnss_driver::ToString(*result.plan.signal_profile) << "\n";
  }
  if (result.plan.vendor == "ublox")
  {
    if (!result.plan.output_port.has_value())
    {
      output << "Output port: legacy_default (uart1 + usb)\n";
    }
    else if (*result.plan.output_port == universal_gnss_driver::ReceiverAutoConfigOutputPort::kAuto)
    {
      output << "Output port request: auto\n";
      if (result.plan.resolved_output_port.has_value())
      {
        output << "Resolved output port: "
               << universal_gnss_driver::ToString(*result.plan.resolved_output_port) << "\n";
      }
    }
    else
    {
      output << "Output port: "
             << universal_gnss_driver::ToString(
                    result.plan.resolved_output_port.value_or(*result.plan.output_port))
             << "\n";
    }
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

  if (!result.plan.warnings.empty())
  {
    output << "\nWarnings:\n";
    for (const auto& warning : result.plan.warnings)
    {
      output << "- " << warning << "\n";
    }
  }

  if (!result.plan.rollback_expectation.empty())
  {
    output << "\nRollback expectation:\n";
    output << result.plan.rollback_expectation << "\n";
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
  output << "  \"execute_requested\": " << (result.execute_requested ? "true" : "false") << ",\n";
  output << "  \"executed\": " << (result.executed ? "true" : "false") << ",\n";
  output << "  \"profile\": {\n";
  output << "    \"vendor\": \"" << EscapeJson(result.plan.vendor) << "\",\n";
  output << "    \"receiver_family\": \"" << EscapeJson(result.plan.receiver_family) << "\",\n";
  output << "    \"receiver_model\": ";
  if (result.plan.receiver_model.has_value())
  {
    output << "\"" << EscapeJson(*result.plan.receiver_model) << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"name\": \"" << EscapeJson(result.plan.profile) << "\",\n";
  output << "    \"apply_mode\": \"" << EscapeJson(result.plan.apply_mode) << "\",\n";
  output << "    \"persistent\": " << (result.plan.persistent ? "true" : "false") << ",\n";
  output << "    \"signal_profile\": ";
  if (result.plan.signal_profile.has_value())
  {
    output << "\"" << EscapeJson(universal_gnss_driver::ToString(*result.plan.signal_profile))
           << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"output_port\": ";
  if (result.plan.output_port.has_value())
  {
    output << "\"" << EscapeJson(universal_gnss_driver::ToString(*result.plan.output_port)) << "\"";
  }
  else if (result.plan.vendor == "ublox")
  {
    output << "\"legacy_default\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"resolved_output_port\": ";
  if (result.plan.resolved_output_port.has_value())
  {
    output << "\"" << EscapeJson(universal_gnss_driver::ToString(*result.plan.resolved_output_port))
           << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"baud\": ";
  if (result.plan.baud.has_value())
  {
    output << *result.plan.baud;
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"target_configured_baud\": ";
  if (const auto target_baud = ExtractPlannedUnicoreConfigBaud(result.plan.commands);
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
  if (PlanHasFactoryResetCommand(result.plan.commands))
  {
    output << 115200u;
  }
  else
  {
    output << "null";
  }
  output << ",\n";
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
  output << "  \"discovery\": {\n";
  output << "    \"device\": ";
  if (result.plan.detected_device.has_value())
  {
    output << "\"" << EscapeJson(*result.plan.detected_device) << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"stable_id\": ";
  if (result.plan.detected_stable_id.has_value())
  {
    output << "\"" << EscapeJson(*result.plan.detected_stable_id) << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"baud\": ";
  if (result.plan.detected_baud.has_value())
  {
    output << *result.plan.detected_baud;
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"confidence\": ";
  if (result.plan.discovery_confidence.has_value())
  {
    output << "\"" << EscapeJson(*result.plan.discovery_confidence) << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "    \"score\": ";
  if (result.plan.discovery_score.has_value())
  {
    output << *result.plan.discovery_score;
  }
  else
  {
    output << "null";
  }
  output << "\n";
  output << "  },\n";
  output << "  \"transport\": {\n";
  output << "    \"device\": \"" << EscapeJson(result.device_path) << "\",\n";
  output << "    \"baud\": " << result.transport_baud_rate << ",\n";
  output << "    \"timeout_ms\": " << result.timeout_ms << "\n";
  output << "  },\n";
  output << "  \"safety\": {\n";
  output << "    \"runtime_confirmation_required\": "
         << (result.requires_runtime_confirmation ? "true" : "false") << ",\n";
  output << "    \"persistent_confirmation_required\": "
         << (result.requires_persistent_confirmation ? "true" : "false") << ",\n";
  output << "    \"execution_confirmed\": " << (result.execution_confirmed ? "true" : "false")
         << "\n";
  output << "  },\n";
  output << "  \"validation\": {\n";
  output << "    \"receiver_recognized\": " << (result.plan.receiver_recognized ? "true" : "false")
         << ",\n";
  output << "    \"config_supported\": " << (result.plan.config_supported ? "true" : "false")
         << ",\n";
  output << "    \"profile_supported\": " << (result.plan.profile_supported ? "true" : "false")
         << ",\n";
  output << "    \"apply_mode_supported\": "
         << (result.plan.apply_mode_supported ? "true" : "false") << ",\n";
  output << "    \"production_ready\": " << (result.plan.production_ready ? "true" : "false")
         << ",\n";
  output << "    \"ready_to_execute\": " << (result.plan.ready_to_execute ? "true" : "false")
         << "\n";
  output << "  },\n";
  output << "  \"plan_summary\": {\n";
  output << "    \"commands\": " << result.plan.summary.commands_total << ",\n";
  output << "    \"runtime\": " << result.plan.summary.runtime_commands << ",\n";
  output << "    \"persistent\": " << result.plan.summary.persistent_commands << ",\n";
  output << "    \"factory_reset\": " << result.plan.summary.factory_reset_commands << "\n";
  output << "  },\n";
  AppendCommandSequenceJson(output, result);
  output << "  \"warnings\": [\n";
  for (std::size_t index = 0; index < result.plan.warnings.size(); ++index)
  {
    output << "    \"" << EscapeJson(result.plan.warnings[index]) << "\"";
    if (index + 1u != result.plan.warnings.size())
    {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"rollback_expectation\": \"" << EscapeJson(result.plan.rollback_expectation)
         << "\",\n";
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
  output << "    \"final_status\": \"" << EscapeJson(result.execution_summary.final_status)
         << "\"\n";
  output << "  },\n";
  output << "  \"error_message\": \"" << EscapeJson(result.error_message) << "\"\n";
  output << "}\n";
  return output.str();
}

}  // namespace universal_gnss_tools
