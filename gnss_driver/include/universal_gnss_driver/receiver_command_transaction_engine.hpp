#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "universal_gnss_driver/receiver_command_dispatcher.hpp"
#include "universal_gnss_driver/receiver_command_transaction.hpp"
#include "universal_gnss_driver/ubx_command_response_mapper.hpp"
#include "universal_gnss_transport/byte_stream.hpp"

namespace universal_gnss_driver
{

struct ReceiverCommandResponseMatchMetadata
{
  std::optional<UbxMessageIdentity> ubx_target{};
};

enum class ReceiverCommandTransactionEngineStepStatus : std::uint8_t
{
  kIdle = 0,
  kDispatched = 1,
  kBusy = 2,
  kDispatchFailed = 3,
  kAcknowledged = 4,
  kRejected = 5,
  kResponseUnmatched = 6,
  kTimedOut = 7,
  kRetryDispatched = 8,
  kRetryUnavailable = 9,
  kNoCurrentTransaction = 10,
  kNotTimedOut = 11,
};

struct EngineStepResult
{
  ReceiverCommandTransactionEngineStepStatus status{
      ReceiverCommandTransactionEngineStepStatus::kIdle};
  bool response_matched{false};
  std::optional<DispatchResult> dispatch_result{};
  std::string error_message{};
};

struct ReceiverCommandTransactionEngineConfig
{
  ReceiverCommandDispatcherConfig dispatcher_config{};
  bool verify_response_metadata{true};
};

struct ReceiverCommandTransactionEngineMetrics
{
  std::size_t transactions_created{0u};
  std::size_t commands_dispatched{0u};
  std::size_t responses_accepted{0u};
  std::size_t responses_unmatched{0u};
  std::size_t transactions_acknowledged{0u};
  std::size_t transactions_rejected{0u};
  std::size_t transactions_timed_out{0u};
  std::size_t dispatch_failures{0u};
};

class ReceiverCommandTransactionEngine
{
public:
  ReceiverCommandTransactionEngine(universal_gnss_transport::ByteSink& sink,
                                   ReceiverCommandTransactionEngineConfig config = {});

  EngineStepResult StartTransaction(
      const ReceiverCommand& command,
      std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt);

  EngineStepResult ApplyResponse(
      const ReceiverCommandResponse& response,
      const ReceiverCommandResponseMatchMetadata& match_metadata = {});

  EngineStepResult MarkTimeout(
      std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt);

  EngineStepResult CheckTimeout(ReceiverCommandTimestampNs now_timestamp_ns);

  EngineStepResult RetryPending(
      std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt);

  void Reset();

  const ReceiverCommandTransactionEngineConfig& config() const;

  const ReceiverCommandTransactionEngineMetrics& metrics() const;

  const std::optional<ReceiverCommandTransaction>& current_transaction() const;

  const std::optional<ReceiverCommandTransaction>& completed_transaction() const;

  const ReceiverCommandDispatcher& dispatcher() const;

private:
  DispatchResult DispatchCommand(const ReceiverCommand& command);

  void MarkFailed(ReceiverCommandTransaction& transaction,
                  const DispatchResult& dispatch_result,
                  std::optional<ReceiverCommandTimestampNs> timestamp_ns);

  bool ResponseMatchesCurrent(
      const ReceiverCommandResponseMatchMetadata& match_metadata) const;

  static bool CanApplyResponseKind(ReceiverCommandResponseKind kind);

  ReceiverCommandDispatcher dispatcher_;
  ReceiverCommandTransactionEngineConfig config_{};
  ReceiverCommandTransactionEngineMetrics metrics_{};
  std::optional<ReceiverCommandTransaction> current_transaction_{};
  std::optional<ReceiverCommandTransaction> completed_transaction_{};
  ReceiverCommandTransactionId next_transaction_id_{1u};
};

}  // namespace universal_gnss_driver
