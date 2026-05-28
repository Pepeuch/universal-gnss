#include "universal_gnss_driver/receiver_command_dispatcher.hpp"

#include <cstddef>
#include <cstdint>

namespace universal_gnss_driver
{

namespace
{

struct PayloadView
{
  const std::uint8_t* data{nullptr};
  std::size_t size{0u};
};

PayloadView GetPayloadView(const ReceiverCommandPayload& payload)
{
  switch (payload.kind)
  {
    case ReceiverCommandPayloadKind::kNone:
      return PayloadView{};
    case ReceiverCommandPayloadKind::kBinary:
      return PayloadView{payload.binary.data(), payload.binary.size()};
    case ReceiverCommandPayloadKind::kText:
      return PayloadView{
          reinterpret_cast<const std::uint8_t*>(payload.text.data()),
          payload.text.size()};
  }

  return PayloadView{};
}

DispatchResult MakeRejectedResult(const DispatchStatus status, const char* message)
{
  DispatchResult result;
  result.status = status;
  result.error_message = message;
  return result;
}

}  // namespace

ReceiverCommandDispatcher::ReceiverCommandDispatcher(
    universal_gnss_transport::ByteSink& sink,
    ReceiverCommandDispatcherConfig config)
    : sink_(sink), config_(config)
{
}

DispatchResult ReceiverCommandDispatcher::Dispatch(const ReceiverCommand& command)
{
  ++metrics_.commands_attempted;

  if (command.kind == ReceiverCommandKind::kUnknown)
  {
    ++metrics_.commands_rejected_invalid;
    return MakeRejectedResult(DispatchStatus::kRejectedInvalid,
                              "receiver command kind must be set before dispatch");
  }

  if (!HasSafeDispatchApproval(command))
  {
    ++metrics_.commands_rejected_safety;
    return MakeRejectedResult(DispatchStatus::kRejectedSafety,
                              "receiver command requires explicit safety confirmation");
  }

  const PayloadView payload = GetPayloadView(command.payload);
  if (payload.size == 0u && !config_.allow_empty_payload_dispatch)
  {
    ++metrics_.commands_rejected_invalid;
    return MakeRejectedResult(DispatchStatus::kRejectedInvalid,
                              "receiver command payload is empty");
  }

  DispatchResult result;
  result.status = DispatchStatus::kSent;

  std::size_t offset = 0u;
  while (offset < payload.size)
  {
    const auto write_result = sink_.Write(payload.data + offset, payload.size - offset);
    result.transport_status = write_result.status;
    result.transport_error = write_result.error;
    result.bytes_written += write_result.bytes_written;

    if (write_result.status != universal_gnss_transport::TransportStatus::kOk)
    {
      ++metrics_.write_errors;
      metrics_.bytes_written += result.bytes_written;
      result.status = DispatchStatus::kWriteError;
      result.error_message = "transport write failed";
      return result;
    }

    if (write_result.bytes_written == 0u)
    {
      ++metrics_.write_errors;
      metrics_.bytes_written += result.bytes_written;
      result.status = DispatchStatus::kWriteError;
      result.transport_status = universal_gnss_transport::TransportStatus::kError;
      result.transport_error = universal_gnss_transport::TransportError::kWriteFailure;
      result.error_message = "transport write made no progress";
      return result;
    }

    offset += write_result.bytes_written;
  }

  ++metrics_.commands_sent;
  metrics_.bytes_written += result.bytes_written;
  result.error_message.clear();
  return result;
}

void ReceiverCommandDispatcher::ResetMetrics()
{
  metrics_ = ReceiverCommandDispatcherMetrics{};
}

const ReceiverCommandDispatcherConfig& ReceiverCommandDispatcher::config() const
{
  return config_;
}

const ReceiverCommandDispatcherMetrics& ReceiverCommandDispatcher::metrics() const
{
  return metrics_;
}

}  // namespace universal_gnss_driver
