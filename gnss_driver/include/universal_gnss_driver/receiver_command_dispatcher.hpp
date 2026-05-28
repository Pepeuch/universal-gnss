#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/transport_error.hpp"
#include "universal_gnss_transport/transport_status.hpp"

namespace universal_gnss_driver
{

enum class DispatchStatus : std::uint8_t
{
  kSent = 0,
  kRejectedSafety = 1,
  kRejectedInvalid = 2,
  kWriteError = 3,
};

struct DispatchResult
{
  DispatchStatus status{DispatchStatus::kRejectedInvalid};
  std::size_t bytes_written{0u};
  universal_gnss_transport::TransportStatus transport_status{
      universal_gnss_transport::TransportStatus::kOk};
  universal_gnss_transport::TransportError transport_error{
      universal_gnss_transport::TransportError::kNone};
  std::string error_message{};
};

struct ReceiverCommandDispatcherConfig
{
  bool allow_empty_payload_dispatch{false};
};

struct ReceiverCommandDispatcherMetrics
{
  std::size_t commands_attempted{0u};
  std::size_t commands_sent{0u};
  std::size_t commands_rejected_safety{0u};
  std::size_t commands_rejected_invalid{0u};
  std::size_t bytes_written{0u};
  std::size_t write_errors{0u};
};

class ReceiverCommandDispatcher
{
public:
  ReceiverCommandDispatcher(universal_gnss_transport::ByteSink& sink,
                            ReceiverCommandDispatcherConfig config = {});

  DispatchResult Dispatch(const ReceiverCommand& command);

  void ResetMetrics();

  const ReceiverCommandDispatcherConfig& config() const;

  const ReceiverCommandDispatcherMetrics& metrics() const;

private:
  universal_gnss_transport::ByteSink& sink_;
  ReceiverCommandDispatcherConfig config_{};
  ReceiverCommandDispatcherMetrics metrics_{};
};

}  // namespace universal_gnss_driver
