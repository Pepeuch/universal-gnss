#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command_transaction_engine.hpp"
#include "universal_gnss_transport/byte_stream.hpp"

namespace universal_gnss_driver
{

enum class ReceiverConfigApplicationState : std::uint8_t
{
  kIdle = 0,
  kRunning = 1,
  kWaitingForResponse = 2,
  kCompleted = 3,
  kFailed = 4,
};

struct ReceiverConfigApplicationConfig
{
  ReceiverCommandTransactionEngineConfig transaction_engine{};
  bool continue_on_error{false};
};

struct ReceiverConfigApplicationMetrics
{
  std::size_t commands_total{0u};
  std::size_t commands_started{0u};
  std::size_t commands_completed{0u};
  std::size_t commands_failed{0u};
  std::size_t commands_retried{0u};
  std::size_t responses_applied{0u};
  std::size_t timeouts_seen{0u};
};

struct ReceiverConfigApplicationResult
{
  ReceiverConfigApplicationState state{ReceiverConfigApplicationState::kIdle};
  std::size_t command_index{0u};
  bool command_started{false};
  bool command_finished{false};
  bool advanced_to_next_command{false};
  bool response_applied{false};
  bool retry_dispatched{false};
  std::string error_message{};
  std::optional<EngineStepResult> engine_result{};
};

class ReceiverConfigApplication
{
public:
  ReceiverConfigApplication(universal_gnss_transport::ByteSink& sink,
                            ReceiverConfigApplicationConfig config = {});

  ReceiverConfigApplicationResult Start(
      std::vector<ReceiverCommand> commands,
      std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt);

  ReceiverConfigApplicationResult Step(
      std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt);

  ReceiverConfigApplicationResult ApplyResponse(
      const ReceiverCommandResponse& response,
      const ReceiverCommandResponseMatchMetadata& match_metadata = {});

  ReceiverConfigApplicationResult MarkTimeout(
      std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt);

  ReceiverConfigApplicationResult CheckTimeout(ReceiverCommandTimestampNs now_timestamp_ns);

  void Reset();

  const ReceiverConfigApplicationConfig& config() const;

  const ReceiverConfigApplicationMetrics& metrics() const;

  ReceiverConfigApplicationState state() const;

  std::size_t command_count() const;

  std::size_t current_index() const;

  const ReceiverCommand* current_command() const;

  const ReceiverCommandTransactionEngine& transaction_engine() const;

private:
  ReceiverConfigApplicationResult BuildResult() const;

  ReceiverConfigApplicationResult FailApplication(const EngineStepResult& engine_result,
                                                  const char* fallback_error_message);

  ReceiverConfigApplicationResult HandleTimeoutResult(
      const EngineStepResult& timeout_result,
      std::optional<ReceiverCommandTimestampNs> retry_timestamp_ns);

  void ResetRunState();

  ReceiverCommandTransactionEngine engine_;
  ReceiverConfigApplicationConfig config_{};
  ReceiverConfigApplicationMetrics metrics_{};
  std::vector<ReceiverCommand> commands_{};
  ReceiverConfigApplicationState state_{ReceiverConfigApplicationState::kIdle};
  std::size_t current_index_{0u};
};

}  // namespace universal_gnss_driver
