#pragma once

#include <cstdint>
#include <optional>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_command_response.hpp"

namespace universal_gnss_driver
{

enum class ReceiverCommandTransactionState : std::uint8_t
{
  kPending = 0,
  kSent = 1,
  kAcknowledged = 2,
  kRejected = 3,
  kTimedOut = 4,
  kFailed = 5,
};

using ReceiverCommandTransactionId = std::uint64_t;

struct ReceiverCommandTransaction
{
  ReceiverCommandTransactionId transaction_id{0u};
  ReceiverCommand command{};
  ReceiverCommandTransactionState state{ReceiverCommandTransactionState::kPending};
  ReceiverCommandResponse response{};
  std::optional<ReceiverCommandTimestampNs> created_timestamp_ns{};
  std::optional<ReceiverCommandTimestampNs> sent_timestamp_ns{};
  std::optional<ReceiverCommandTimestampNs> completed_timestamp_ns{};
  std::uint32_t attempt_count{0u};

  constexpr std::uint8_t max_retries() const
  {
    return command.retry_policy.max_retries;
  }

  void mark_sent(const std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt)
  {
    state = ReceiverCommandTransactionState::kSent;
    ++attempt_count;
    sent_timestamp_ns = timestamp_ns;
    completed_timestamp_ns = std::nullopt;
    response = ReceiverCommandResponse{};
  }

  void mark_ack(
      const std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt,
      const ReceiverCommandResponseKind response_kind = ReceiverCommandResponseKind::kAck)
  {
    state = ReceiverCommandTransactionState::kAcknowledged;
    response.kind = IsPositiveReceiverCommandResponseKind(response_kind)
                        ? response_kind
                        : ReceiverCommandResponseKind::kAck;
    response.timestamp_ns = timestamp_ns;
    completed_timestamp_ns = timestamp_ns;
  }

  void mark_nak(
      const std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt,
      const ReceiverCommandResponseKind response_kind = ReceiverCommandResponseKind::kNak)
  {
    state = ReceiverCommandTransactionState::kRejected;
    response.kind = response_kind == ReceiverCommandResponseKind::kTextError
                        ? ReceiverCommandResponseKind::kTextError
                        : ReceiverCommandResponseKind::kNak;
    response.timestamp_ns = timestamp_ns;
    completed_timestamp_ns = timestamp_ns;
  }

  void mark_timeout(
      const std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt)
  {
    state = ReceiverCommandTransactionState::kTimedOut;
    response.kind = ReceiverCommandResponseKind::kTimeout;
    response.timestamp_ns = timestamp_ns;
    completed_timestamp_ns = timestamp_ns;
  }

  constexpr bool can_retry() const
  {
    if (state != ReceiverCommandTransactionState::kTimedOut &&
        state != ReceiverCommandTransactionState::kFailed)
    {
      return false;
    }

    return attempt_count > 0u && attempt_count <= max_retries();
  }

  void reset_for_retry()
  {
    if (!can_retry())
    {
      return;
    }

    state = ReceiverCommandTransactionState::kPending;
    response = ReceiverCommandResponse{};
    sent_timestamp_ns = std::nullopt;
    completed_timestamp_ns = std::nullopt;
  }
};

constexpr bool IsReceiverCommandTransactionTerminal(
    const ReceiverCommandTransactionState state)
{
  return state == ReceiverCommandTransactionState::kAcknowledged ||
         state == ReceiverCommandTransactionState::kRejected ||
         state == ReceiverCommandTransactionState::kTimedOut ||
         state == ReceiverCommandTransactionState::kFailed;
}

}  // namespace universal_gnss_driver
