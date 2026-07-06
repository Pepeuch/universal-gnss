#include "universal_gnss_driver/receiver_config_application.hpp"

namespace universal_gnss_driver
{

namespace
{

EngineStepResult MakeApplicationEngineResult(
    const ReceiverCommandTransactionEngineStepStatus status, const char* error_message)
{
  EngineStepResult result;
  result.status = status;
  result.error_message = error_message;
  return result;
}

}  // namespace

ReceiverConfigApplication::ReceiverConfigApplication(universal_gnss_transport::ByteSink& sink,
                                                     ReceiverConfigApplicationConfig config)
    : engine_(sink, config.transaction_engine), config_(config)
{
}

ReceiverConfigApplicationResult ReceiverConfigApplication::Start(
    std::vector<ReceiverCommand> commands,
    const std::optional<ReceiverCommandTimestampNs> timestamp_ns)
{
  if (state_ == ReceiverConfigApplicationState::kRunning ||
      state_ == ReceiverConfigApplicationState::kWaitingForResponse)
  {
    ReceiverConfigApplicationResult result = BuildResult();
    result.error_message = "configuration application is already active";
    result.engine_result =
        MakeApplicationEngineResult(ReceiverCommandTransactionEngineStepStatus::kBusy,
                                    "configuration application is already active");
    return result;
  }

  ResetRunState();
  commands_ = std::move(commands);
  metrics_.commands_total = commands_.size();

  if (commands_.empty())
  {
    state_ = ReceiverConfigApplicationState::kCompleted;
    return BuildResult();
  }

  state_ = ReceiverConfigApplicationState::kRunning;
  return Step(timestamp_ns);
}

ReceiverConfigApplicationResult ReceiverConfigApplication::Step(
    const std::optional<ReceiverCommandTimestampNs> timestamp_ns)
{
  if (state_ == ReceiverConfigApplicationState::kIdle)
  {
    ReceiverConfigApplicationResult result = BuildResult();
    result.error_message = "configuration application is idle";
    result.engine_result =
        MakeApplicationEngineResult(ReceiverCommandTransactionEngineStepStatus::kIdle,
                                    "configuration application is idle");
    return result;
  }

  if (state_ == ReceiverConfigApplicationState::kWaitingForResponse)
  {
    ReceiverConfigApplicationResult result = BuildResult();
    result.error_message = "configuration application is waiting for a response";
    result.engine_result =
        MakeApplicationEngineResult(ReceiverCommandTransactionEngineStepStatus::kBusy,
                                    "configuration application is waiting for a response");
    return result;
  }

  if (state_ == ReceiverConfigApplicationState::kCompleted ||
      state_ == ReceiverConfigApplicationState::kFailed)
  {
    return BuildResult();
  }

  if (current_index_ >= commands_.size())
  {
    state_ = ReceiverConfigApplicationState::kCompleted;
    return BuildResult();
  }

  const auto engine_result = engine_.StartTransaction(commands_[current_index_], timestamp_ns);

  ++metrics_.commands_started;

  if (engine_result.status == ReceiverCommandTransactionEngineStepStatus::kAcknowledged)
  {
    ReceiverConfigApplicationResult result = CompleteCurrentCommand(
        engine_result, true, false, "configuration command completed without a response");
    result.command_started = true;
    return result;
  }

  if (engine_result.status != ReceiverCommandTransactionEngineStepStatus::kDispatched)
  {
    ReceiverConfigApplicationResult result =
        HandleCommandFailure(engine_result, false, "failed to dispatch configuration command");
    result.command_started = true;
    return result;
  }

  state_ = ReceiverConfigApplicationState::kWaitingForResponse;
  ReceiverConfigApplicationResult result = BuildResult();
  result.command_started = true;
  result.engine_result = engine_result;
  return result;
}

ReceiverConfigApplicationResult ReceiverConfigApplication::ApplyResponse(
    const ReceiverCommandResponse& response,
    const ReceiverCommandResponseMatchMetadata& match_metadata)
{
  const auto engine_result = engine_.ApplyResponse(response, match_metadata);

  ReceiverConfigApplicationResult result = BuildResult();
  result.engine_result = engine_result;

  if (engine_result.status == ReceiverCommandTransactionEngineStepStatus::kResponseUnmatched)
  {
    result.error_message = engine_result.error_message;
    return result;
  }

  if (engine_result.status != ReceiverCommandTransactionEngineStepStatus::kAcknowledged &&
      engine_result.status != ReceiverCommandTransactionEngineStepStatus::kRejected)
  {
    result.error_message = engine_result.error_message;
    return result;
  }

  result.response_applied = true;
  ++metrics_.responses_applied;

  const bool command_succeeded =
      engine_result.status == ReceiverCommandTransactionEngineStepStatus::kAcknowledged;

  if (command_succeeded)
  {
    return CompleteCurrentCommand(engine_result,
                                  true,
                                  true,
                                  "configuration command completed successfully");
  }

  const std::string completed_message = engine_.completed_transaction().has_value()
                                            ? engine_.completed_transaction()->response.message
                                            : std::string{};
  return HandleCommandFailure(engine_result,
                              true,
                              "configuration command was rejected",
                              completed_message);
}

ReceiverConfigApplicationResult ReceiverConfigApplication::MarkTimeout(
    const std::optional<ReceiverCommandTimestampNs> timestamp_ns)
{
  return HandleTimeoutResult(engine_.MarkTimeout(timestamp_ns), timestamp_ns);
}

ReceiverConfigApplicationResult ReceiverConfigApplication::CheckTimeout(
    const ReceiverCommandTimestampNs now_timestamp_ns)
{
  return HandleTimeoutResult(engine_.CheckTimeout(now_timestamp_ns), now_timestamp_ns);
}

void ReceiverConfigApplication::Reset()
{
  ResetRunState();
}

const ReceiverConfigApplicationConfig& ReceiverConfigApplication::config() const
{
  return config_;
}

const ReceiverConfigApplicationMetrics& ReceiverConfigApplication::metrics() const
{
  return metrics_;
}

ReceiverConfigApplicationState ReceiverConfigApplication::state() const
{
  return state_;
}

std::size_t ReceiverConfigApplication::command_count() const
{
  return commands_.size();
}

std::size_t ReceiverConfigApplication::current_index() const
{
  return current_index_;
}

const ReceiverCommand* ReceiverConfigApplication::current_command() const
{
  if (current_index_ >= commands_.size())
  {
    return nullptr;
  }

  return &commands_[current_index_];
}

const ReceiverCommandTransactionEngine& ReceiverConfigApplication::transaction_engine() const
{
  return engine_;
}

ReceiverConfigApplicationResult ReceiverConfigApplication::BuildResult() const
{
  ReceiverConfigApplicationResult result;
  result.state = state_;
  result.command_index = current_index_;
  return result;
}

ReceiverConfigApplicationResult ReceiverConfigApplication::CompleteCurrentCommand(
    const EngineStepResult& engine_result,
    const bool command_succeeded,
    const bool response_applied,
    const char* fallback_error_message,
    std::string error_message)
{
  const ReceiverCommand* command = current_command();
  const bool command_required = command == nullptr || IsRequiredCommand(*command);
  const bool command_failed = !command_succeeded;

  if (command_succeeded)
  {
    ++metrics_.commands_completed;
  }
  else
  {
    ++metrics_.commands_failed;
    if (command_required)
    {
      ++metrics_.required_commands_failed;
    }
    else
    {
      ++metrics_.optional_commands_failed;
    }
  }

  const bool should_continue = !command_failed || config_.continue_on_error ||
                               (command != nullptr && !IsRequiredCommand(*command));
  const bool has_more_commands = (current_index_ + 1u) < commands_.size();

  if (should_continue)
  {
    ++current_index_;
    state_ = has_more_commands ? ReceiverConfigApplicationState::kRunning
                               : ReceiverConfigApplicationState::kCompleted;
  }
  else
  {
    state_ = ReceiverConfigApplicationState::kFailed;
  }

  if (error_message.empty())
  {
    error_message =
        engine_result.error_message.empty() ? fallback_error_message : engine_result.error_message;
  }

  ReceiverConfigApplicationResult result = BuildResult();
  result.command_finished = true;
  result.command_succeeded = command_succeeded;
  result.command_failed = command_failed;
  result.command_required = command_required;
  result.failure_ignored = command_failed && should_continue;
  result.advanced_to_next_command = should_continue && has_more_commands;
  result.response_applied = response_applied;
  result.engine_result = engine_result;
  result.error_message = std::move(error_message);
  return result;
}

ReceiverConfigApplicationResult ReceiverConfigApplication::HandleCommandFailure(
    const EngineStepResult& engine_result,
    const bool response_applied,
    const char* fallback_error_message,
    std::string error_message)
{
  return CompleteCurrentCommand(
      engine_result, false, response_applied, fallback_error_message, std::move(error_message));
}

ReceiverConfigApplicationResult ReceiverConfigApplication::HandleTimeoutResult(
    const EngineStepResult& timeout_result,
    const std::optional<ReceiverCommandTimestampNs> retry_timestamp_ns)
{
  ReceiverConfigApplicationResult result = BuildResult();
  result.engine_result = timeout_result;

  if (timeout_result.status != ReceiverCommandTransactionEngineStepStatus::kTimedOut)
  {
    result.error_message = timeout_result.error_message;
    return result;
  }

  ++metrics_.timeouts_seen;

  if (!engine_.current_transaction().has_value() || !engine_.current_transaction()->can_retry())
  {
    return HandleCommandFailure(timeout_result,
                                false,
                                "configuration command timed out without retry budget");
  }

  const auto retry_result = engine_.RetryPending(retry_timestamp_ns);
  if (retry_result.status != ReceiverCommandTransactionEngineStepStatus::kRetryDispatched)
  {
    return HandleCommandFailure(retry_result, false, "configuration command retry dispatch failed");
  }

  ++metrics_.commands_retried;
  state_ = ReceiverConfigApplicationState::kWaitingForResponse;

  ReceiverConfigApplicationResult retried = BuildResult();
  retried.retry_dispatched = true;
  retried.engine_result = retry_result;
  return retried;
}

void ReceiverConfigApplication::ResetRunState()
{
  engine_.Reset();
  metrics_ = ReceiverConfigApplicationMetrics{};
  commands_.clear();
  state_ = ReceiverConfigApplicationState::kIdle;
  current_index_ = 0u;
}

}  // namespace universal_gnss_driver
