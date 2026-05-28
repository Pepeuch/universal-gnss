#pragma once

#include <cstddef>
#include <cstdint>

#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/transport_error.hpp"
#include "universal_gnss_transport/transport_status.hpp"

namespace universal_gnss_driver
{

struct ReceiverSessionRunnerConfig
{
  std::size_t read_chunk_size{512u};
  bool finalize_session_on_end_of_stream{true};
  bool finalize_session_on_closed{true};
  bool finalize_session_on_error{false};
};

struct ReceiverSessionRunnerMetrics
{
  std::size_t bytes_read{0u};
  std::size_t chunks_read{0u};
  bool eof_seen{false};
  std::size_t read_errors{0u};
  std::size_t runtime_updates_observed{0u};
  universal_gnss_transport::TransportStatus last_status{
      universal_gnss_transport::TransportStatus::kOk};
  universal_gnss_transport::TransportError last_error{
      universal_gnss_transport::TransportError::kNone};
};

class ReceiverSessionRunner
{
public:
  ReceiverSessionRunner(universal_gnss_transport::ByteSource& source,
                        ReceiverSession& session,
                        ReceiverSessionRunnerConfig config = {});

  bool StepOnce();

  void RunUntilEof();

  void ResetMetrics();

  const ReceiverSessionRunnerMetrics& metrics() const;

  const ReceiverSessionRunnerConfig& config() const;

private:
  void NoteRuntimeUpdateDelta(std::size_t before_runtime_updates);
  void FinalizeSessionForTerminalStatus(universal_gnss_transport::TransportStatus status);

  universal_gnss_transport::ByteSource& source_;
  ReceiverSession& session_;
  ReceiverSessionRunnerConfig config_{};
  ReceiverSessionRunnerMetrics metrics_{};
};

}  // namespace universal_gnss_driver
