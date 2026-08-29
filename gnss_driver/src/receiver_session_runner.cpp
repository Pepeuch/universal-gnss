#include "universal_gnss_driver/receiver_session_runner.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace universal_gnss_driver
{

namespace
{

std::size_t NormalizeReadChunkSize(const std::size_t configured_size)
{
  return std::max<std::size_t>(configured_size, 1u);
}

std::int64_t MonotonicNowNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

ReceiverSessionRunner::ReceiverSessionRunner(universal_gnss_transport::ByteSource& source,
                                             ReceiverSession& session,
                                             ReceiverSessionRunnerConfig config)
    : source_(source), session_(session), config_(config)
{
  config_.read_chunk_size = NormalizeReadChunkSize(config_.read_chunk_size);
}

bool ReceiverSessionRunner::StepOnce()
{
  std::vector<std::uint8_t> buffer(config_.read_chunk_size, 0u);
  const auto read_result = source_.Read(buffer.data(), buffer.size());
  metrics_.last_status = read_result.status;
  metrics_.last_error = read_result.error;

  if (read_result.status == universal_gnss_transport::TransportStatus::kOk)
  {
    if (read_result.bytes_read == 0u)
    {
      return false;
    }

    ++metrics_.chunks_read;
    metrics_.bytes_read += read_result.bytes_read;

    const std::int64_t receipt_timestamp_ns = config_.receipt_timestamp_provider
                                                  ? config_.receipt_timestamp_provider()
                                                  : MonotonicNowNs();
    const std::size_t before_runtime_updates = session_.metrics().runtime_updates;
    session_.FeedBytes(buffer.data(), read_result.bytes_read, receipt_timestamp_ns);
    NoteRuntimeUpdateDelta(before_runtime_updates);
    return true;
  }

  if (read_result.status == universal_gnss_transport::TransportStatus::kEndOfStream)
  {
    metrics_.eof_seen = true;
  }
  else if (read_result.status == universal_gnss_transport::TransportStatus::kError)
  {
    ++metrics_.read_errors;
  }

  FinalizeSessionForTerminalStatus(read_result.status);
  return false;
}

void ReceiverSessionRunner::RunUntilEof()
{
  while (StepOnce())
  {
  }
}

void ReceiverSessionRunner::ResetMetrics()
{
  metrics_ = ReceiverSessionRunnerMetrics{};
}

const ReceiverSessionRunnerMetrics& ReceiverSessionRunner::metrics() const
{
  return metrics_;
}

const ReceiverSessionRunnerConfig& ReceiverSessionRunner::config() const
{
  return config_;
}

void ReceiverSessionRunner::NoteRuntimeUpdateDelta(const std::size_t before_runtime_updates)
{
  const std::size_t after_runtime_updates = session_.metrics().runtime_updates;
  if (after_runtime_updates > before_runtime_updates)
  {
    metrics_.runtime_updates_observed += after_runtime_updates - before_runtime_updates;
  }
}

void ReceiverSessionRunner::FinalizeSessionForTerminalStatus(
    const universal_gnss_transport::TransportStatus status)
{
  if ((status == universal_gnss_transport::TransportStatus::kEndOfStream &&
       config_.finalize_session_on_end_of_stream) ||
      (status == universal_gnss_transport::TransportStatus::kClosed &&
       config_.finalize_session_on_closed) ||
      (status == universal_gnss_transport::TransportStatus::kError &&
       config_.finalize_session_on_error))
  {
    const std::size_t before_runtime_updates = session_.metrics().runtime_updates;
    session_.Finalize();
    NoteRuntimeUpdateDelta(before_runtime_updates);
  }
}

}  // namespace universal_gnss_driver
