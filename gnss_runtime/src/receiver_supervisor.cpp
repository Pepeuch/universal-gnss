#include "universal_gnss_runtime/receiver_supervisor.hpp"

#include <algorithm>
#include <utility>

namespace universal_gnss_runtime {
namespace {

const char* ToString(const universal_gnss_transport::TransportStatus status)
{
  using universal_gnss_transport::TransportStatus;
  switch (status)
  {
  case TransportStatus::kOk:
    return "ok";
  case TransportStatus::kEndOfStream:
    return "end_of_stream";
  case TransportStatus::kClosed:
    return "closed";
  case TransportStatus::kError:
    return "error";
  }
  return "unknown";
}

const char* ToString(const universal_gnss_transport::TransportError error)
{
  using universal_gnss_transport::TransportError;
  switch (error)
  {
  case TransportError::kNone:
    return "none";
  case TransportError::kClosed:
    return "closed";
  case TransportError::kInvalidArgument:
    return "invalid_argument";
  case TransportError::kOverflow:
    return "overflow";
  case TransportError::kConnectFailure:
    return "connect_failure";
  case TransportError::kTimeout:
    return "timeout";
  case TransportError::kReadFailure:
    return "read_failure";
  case TransportError::kWriteFailure:
    return "write_failure";
  case TransportError::kUnsupported:
    return "unsupported";
  case TransportError::kUnknown:
    return "unknown";
  case TransportError::kTlsHandshakeFailure:
    return "tls_handshake_failure";
  case TransportError::kTlsVerificationFailure:
    return "tls_verification_failure";
  }
  return "unknown";
}

std::string DescribeTerminalRead(const universal_gnss_driver::ReceiverSessionRunnerMetrics& metrics)
{
  return std::string("read:") + ToString(metrics.last_status) + ":" + ToString(metrics.last_error);
}

} // namespace

const char* ToString(const ReceiverSupervisorLifecycle lifecycle)
{
  switch (lifecycle)
  {
  case ReceiverSupervisorLifecycle::kStopped:
    return "stopped";
  case ReceiverSupervisorLifecycle::kStarting:
    return "starting";
  case ReceiverSupervisorLifecycle::kConnected:
    return "connected";
  case ReceiverSupervisorLifecycle::kReconnecting:
    return "reconnecting";
  case ReceiverSupervisorLifecycle::kStopping:
    return "stopping";
  }
  return "unknown";
}

ReceiverSupervisor::ReceiverSupervisor(ReceiverSupervisorConfig config) : config_(std::move(config))
{
  config_.initial_reconnect_backoff =
      std::max(config_.initial_reconnect_backoff, std::chrono::milliseconds(1));
  config_.maximum_reconnect_backoff =
      std::max(config_.maximum_reconnect_backoff, config_.initial_reconnect_backoff);
}

ReceiverSupervisor::~ReceiverSupervisor() { Stop(); }

bool ReceiverSupervisor::Start()
{
  if (!config_.transport_factory || worker_.joinable())
  {
    return false;
  }

  stopping_ = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = ReceiverSupervisorSnapshot{};
    snapshot_.lifecycle = ReceiverSupervisorLifecycle::kStarting;
  }
  worker_ = std::thread(&ReceiverSupervisor::Run, this);
  return true;
}

void ReceiverSupervisor::Stop()
{
  stopping_ = true;
  std::shared_ptr<universal_gnss_transport::ByteDuplex> transport;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.lifecycle != ReceiverSupervisorLifecycle::kStopped)
    {
      snapshot_.lifecycle = ReceiverSupervisorLifecycle::kStopping;
    }
    transport = active_;
  }
  condition_.notify_all();
  if (transport)
  {
    static_cast<universal_gnss_transport::ByteSource&>(*transport).Close();
  }
  if (worker_.joinable())
  {
    worker_.join();
  }
}

ReceiverSupervisorSnapshot ReceiverSupervisor::Snapshot() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

void ReceiverSupervisor::Run()
{
  auto delay = config_.initial_reconnect_backoff;
  while (!stopping_)
  {
    auto opened = config_.transport_factory();
    if (!opened.transport ||
        !static_cast<universal_gnss_transport::ByteSource&>(*opened.transport).IsOpen())
    {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.lifecycle = ReceiverSupervisorLifecycle::kReconnecting;
        snapshot_.connected = false;
        snapshot_.runtime_state.reset();
        // Factory text can contain a device path or implementation detail. Keep
        // the externally visible status redacted and machine-bounded instead.
        snapshot_.last_terminal_error = "open:failed";
        ++snapshot_.reconnect_attempt_count;
      }
      if (!Wait(delay))
        break;
      delay = std::min(config_.maximum_reconnect_backoff, delay * 2);
      continue;
    }

    delay = config_.initial_reconnect_backoff;
    auto transport =
        std::shared_ptr<universal_gnss_transport::ByteDuplex>(std::move(opened.transport));
    universal_gnss_driver::ReceiverSession session(config_.session);
    universal_gnss_driver::ReceiverSessionRunner runner(*transport, session, config_.runner);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_ = transport;
      ++snapshot_.session_incarnation;
      snapshot_.lifecycle = ReceiverSupervisorLifecycle::kConnected;
      snapshot_.connected = true;
      snapshot_.runtime_state.reset();
      snapshot_.session_metrics = session.metrics();
      snapshot_.runner_metrics = runner.metrics();
      snapshot_.last_terminal_error.clear();
    }

    while (!stopping_ && runner.StepOnce())
      Update(session, runner);
    Update(session, runner);
    if (stopping_)
      break;

    static_cast<universal_gnss_transport::ByteSource&>(*transport).Close();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot_.lifecycle = ReceiverSupervisorLifecycle::kReconnecting;
      snapshot_.connected = false;
      snapshot_.runtime_state.reset();
      snapshot_.last_terminal_error = DescribeTerminalRead(runner.metrics());
      ++snapshot_.reconnect_attempt_count;
      active_.reset();
    }
    if (!Wait(delay))
      break;
    delay = std::min(config_.maximum_reconnect_backoff, delay * 2);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.lifecycle = ReceiverSupervisorLifecycle::kStopped;
  snapshot_.connected = false;
  snapshot_.runtime_state.reset();
  active_.reset();
}

void ReceiverSupervisor::Update(const universal_gnss_driver::ReceiverSession& session,
                                const universal_gnss_driver::ReceiverSessionRunner& runner)
{
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.session_metrics = session.metrics();
  snapshot_.runner_metrics = runner.metrics();
  if (session.metrics().runtime_updates != 0u)
  {
    snapshot_.runtime_state = session.current_state();
  }
}

bool ReceiverSupervisor::Wait(const std::chrono::milliseconds delay)
{
  std::unique_lock<std::mutex> lock(mutex_);
  return !condition_.wait_for(lock, delay, [this] { return stopping_.load(); });
}

} // namespace universal_gnss_runtime
