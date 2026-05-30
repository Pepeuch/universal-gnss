#include "universal_gnss_driver/receiver_command_transaction_engine.hpp"

#include <cstdint>

namespace universal_gnss_driver
{

namespace
{

constexpr std::int64_t kNanosecondsPerMillisecond = 1000000LL;

EngineStepResult MakeStepResult(const ReceiverCommandTransactionEngineStepStatus status,
                                const char* error_message = "")
{
  EngineStepResult result;
  result.status = status;
  result.error_message = error_message;
  return result;
}

bool HasTimedOutAt(const ReceiverCommandTransaction& transaction,
                   const ReceiverCommandTimestampNs now_timestamp_ns)
{
  if (!transaction.sent_timestamp_ns.has_value())
  {
    return false;
  }

  const auto timeout_ns = static_cast<std::int64_t>(transaction.command.retry_policy.timeout_ms) *
                          kNanosecondsPerMillisecond;
  return now_timestamp_ns >= (*transaction.sent_timestamp_ns + timeout_ns);
}

}  // namespace

ReceiverCommandTransactionEngine::ReceiverCommandTransactionEngine(
    universal_gnss_transport::ByteSink& sink,
    ReceiverCommandTransactionEngineConfig config)
    : dispatcher_(sink, config.dispatcher_config), config_(config)
{
}

EngineStepResult ReceiverCommandTransactionEngine::StartTransaction(
    const ReceiverCommand& command,
    const std::optional<ReceiverCommandTimestampNs> timestamp_ns)
{
  if (current_transaction_.has_value())
  {
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kBusy,
                          "transaction engine already holds an active transaction slot");
  }

  ReceiverCommandTransaction transaction;
  transaction.transaction_id = next_transaction_id_++;
  transaction.command = command;
  transaction.created_timestamp_ns = timestamp_ns;
  ++metrics_.transactions_created;

  const DispatchResult dispatch_result = DispatchCommand(command);
  EngineStepResult result;
  result.dispatch_result = dispatch_result;

  if (dispatch_result.status != DispatchStatus::kSent)
  {
    MarkFailed(transaction, dispatch_result, timestamp_ns);
    completed_transaction_ = transaction;
    ++metrics_.dispatch_failures;
    result.status = ReceiverCommandTransactionEngineStepStatus::kDispatchFailed;
    result.error_message = dispatch_result.error_message;
    return result;
  }

  transaction.mark_sent(timestamp_ns);
  current_transaction_ = transaction;
  ++metrics_.commands_dispatched;
  result.status = ReceiverCommandTransactionEngineStepStatus::kDispatched;
  return result;
}

EngineStepResult ReceiverCommandTransactionEngine::ApplyResponse(
    const ReceiverCommandResponse& response,
    const ReceiverCommandResponseMatchMetadata& match_metadata)
{
  if (!current_transaction_.has_value())
  {
    ++metrics_.responses_unmatched;
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kResponseUnmatched,
                          "no current transaction is awaiting a response");
  }

  if (current_transaction_->state != ReceiverCommandTransactionState::kSent)
  {
    ++metrics_.responses_unmatched;
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kResponseUnmatched,
                          "current transaction is not awaiting a response");
  }

  if (!CanApplyResponseKind(response.kind))
  {
    ++metrics_.responses_unmatched;
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kResponseUnmatched,
                          "response kind is not applicable to the transaction engine");
  }

  if (!ResponseMatchesCurrent(match_metadata))
  {
    ++metrics_.responses_unmatched;
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kResponseUnmatched,
                          "response metadata does not match the current command");
  }

  EngineStepResult result;
  result.response_matched = true;
  ++metrics_.responses_accepted;

  if (IsPositiveReceiverCommandResponseKind(response.kind))
  {
    current_transaction_->mark_ack(response.timestamp_ns, response.kind);
    current_transaction_->response.message = response.message;
    ++metrics_.transactions_acknowledged;
    completed_transaction_ = *current_transaction_;
    current_transaction_.reset();
    result.status = ReceiverCommandTransactionEngineStepStatus::kAcknowledged;
    return result;
  }

  current_transaction_->mark_nak(response.timestamp_ns, response.kind);
  current_transaction_->response.message = response.message;
  ++metrics_.transactions_rejected;
  completed_transaction_ = *current_transaction_;
  current_transaction_.reset();
  result.status = ReceiverCommandTransactionEngineStepStatus::kRejected;
  return result;
}

EngineStepResult ReceiverCommandTransactionEngine::MarkTimeout(
    const std::optional<ReceiverCommandTimestampNs> timestamp_ns)
{
  if (!current_transaction_.has_value())
  {
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kNoCurrentTransaction,
                          "no current transaction is available to time out");
  }

  if (current_transaction_->state != ReceiverCommandTransactionState::kSent)
  {
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kNotTimedOut,
                          "current transaction is not in a sent state");
  }

  current_transaction_->mark_timeout(timestamp_ns);
  ++metrics_.transactions_timed_out;
  return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kTimedOut);
}

EngineStepResult ReceiverCommandTransactionEngine::CheckTimeout(
    const ReceiverCommandTimestampNs now_timestamp_ns)
{
  if (!current_transaction_.has_value())
  {
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kNoCurrentTransaction,
                          "no current transaction is available for timeout checks");
  }

  if (current_transaction_->state != ReceiverCommandTransactionState::kSent ||
      !HasTimedOutAt(*current_transaction_, now_timestamp_ns))
  {
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kNotTimedOut);
  }

  return MarkTimeout(now_timestamp_ns);
}

EngineStepResult ReceiverCommandTransactionEngine::RetryPending(
    const std::optional<ReceiverCommandTimestampNs> timestamp_ns)
{
  if (!current_transaction_.has_value())
  {
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kNoCurrentTransaction,
                          "no current transaction is available to retry");
  }

  if (!current_transaction_->can_retry())
  {
    return MakeStepResult(ReceiverCommandTransactionEngineStepStatus::kRetryUnavailable,
                          "current transaction is not retryable");
  }

  current_transaction_->reset_for_retry();
  const DispatchResult dispatch_result = DispatchCommand(current_transaction_->command);

  EngineStepResult result;
  result.dispatch_result = dispatch_result;
  if (dispatch_result.status != DispatchStatus::kSent)
  {
    MarkFailed(*current_transaction_, dispatch_result, timestamp_ns);
    ++metrics_.dispatch_failures;
    result.status = ReceiverCommandTransactionEngineStepStatus::kDispatchFailed;
    result.error_message = dispatch_result.error_message;
    return result;
  }

  current_transaction_->mark_sent(timestamp_ns);
  ++metrics_.commands_dispatched;
  result.status = ReceiverCommandTransactionEngineStepStatus::kRetryDispatched;
  return result;
}

void ReceiverCommandTransactionEngine::Reset()
{
  dispatcher_.ResetMetrics();
  metrics_ = ReceiverCommandTransactionEngineMetrics{};
  current_transaction_.reset();
  completed_transaction_.reset();
  next_transaction_id_ = 1u;
}

const ReceiverCommandTransactionEngineConfig& ReceiverCommandTransactionEngine::config() const
{
  return config_;
}

const ReceiverCommandTransactionEngineMetrics& ReceiverCommandTransactionEngine::metrics() const
{
  return metrics_;
}

const std::optional<ReceiverCommandTransaction>&
ReceiverCommandTransactionEngine::current_transaction() const
{
  return current_transaction_;
}

const std::optional<ReceiverCommandTransaction>&
ReceiverCommandTransactionEngine::completed_transaction() const
{
  return completed_transaction_;
}

const ReceiverCommandDispatcher& ReceiverCommandTransactionEngine::dispatcher() const
{
  return dispatcher_;
}

DispatchResult ReceiverCommandTransactionEngine::DispatchCommand(const ReceiverCommand& command)
{
  return dispatcher_.Dispatch(command);
}

void ReceiverCommandTransactionEngine::MarkFailed(
    ReceiverCommandTransaction& transaction,
    const DispatchResult& dispatch_result,
    const std::optional<ReceiverCommandTimestampNs> timestamp_ns)
{
  transaction.state = ReceiverCommandTransactionState::kFailed;
  transaction.response = ReceiverCommandResponse{};
  transaction.response.message = dispatch_result.error_message;
  transaction.response.timestamp_ns = timestamp_ns;
  transaction.completed_timestamp_ns = timestamp_ns;
}

bool ReceiverCommandTransactionEngine::ResponseMatchesCurrent(
    const ReceiverCommandResponseMatchMetadata& match_metadata) const
{
  if (!config_.verify_response_metadata || !match_metadata.ubx_target.has_value())
  {
    return true;
  }

  if (!current_transaction_.has_value())
  {
    return false;
  }

  const auto command_identity =
      TryGetUbxCommandMessageIdentity(current_transaction_->command);
  if (!command_identity.has_value())
  {
    return true;
  }

  return command_identity->class_id == match_metadata.ubx_target->class_id &&
         command_identity->message_id == match_metadata.ubx_target->message_id;
}

bool ReceiverCommandTransactionEngine::CanApplyResponseKind(
    const ReceiverCommandResponseKind kind)
{
  return kind == ReceiverCommandResponseKind::kAck ||
         kind == ReceiverCommandResponseKind::kNak ||
         kind == ReceiverCommandResponseKind::kTextOk ||
         kind == ReceiverCommandResponseKind::kTextError;
}

}  // namespace universal_gnss_driver
