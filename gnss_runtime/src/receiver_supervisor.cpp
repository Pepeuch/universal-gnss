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

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
const char* ToString(const universal_gnss_ntrip::NtripClientError error)
{
  using universal_gnss_ntrip::NtripClientError;
  switch (error)
  {
  case NtripClientError::kNone:
    return "none";
  case NtripClientError::kConfiguration:
    return "configuration";
  case NtripClientError::kAuthentication:
    return "authentication";
  case NtripClientError::kHttp:
    return "http";
  case NtripClientError::kProtocol:
    return "protocol";
  case NtripClientError::kTimeout:
    return "timeout";
  case NtripClientError::kDisconnected:
    return "disconnected";
  case NtripClientError::kUnknown:
    return "unknown";
  }
  return "unknown";
}

universal_gnss::GnssTimestampNs SteadyNowNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
#endif

} // namespace

struct ReceiverSupervisor::ReceiverLink
{
  std::shared_ptr<universal_gnss_transport::ByteDuplex> transport{};
  universal_gnss_transport::RtcmFrameWriter writer{};
  std::uint64_t incarnation{0u};
};

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
  if (!config_.transport_factory || worker_.joinable()
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
      || ntrip_worker_.joinable()
#endif
  )
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
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  if (config_.ntrip.has_value())
  {
    ntrip_worker_ = std::thread(&ReceiverSupervisor::RunNtrip, this);
  }
#endif
  return true;
}

void ReceiverSupervisor::Stop()
{
  stopping_ = true;
  std::shared_ptr<ReceiverLink> link;
  {
    std::lock_guard<std::mutex> lock(correction_mutex_);
    link = active_;
    if (link)
    {
      link->writer.Abandon();
      active_.reset();
    }
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.lifecycle != ReceiverSupervisorLifecycle::kStopped)
    {
      snapshot_.lifecycle = ReceiverSupervisorLifecycle::kStopping;
    }
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
    snapshot_.forwarding_active = false;
    snapshot_.rtcm_forward_queue_depth = 0u;
#endif
  }
  condition_.notify_all();
  if (link)
  {
    static_cast<universal_gnss_transport::ByteSource&>(*link->transport).Close();
  }
  if (worker_.joinable())
  {
    worker_.join();
  }
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  if (ntrip_worker_.joinable())
  {
    ntrip_worker_.join();
  }
#endif
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
    std::uint64_t incarnation = 0u;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      incarnation = ++snapshot_.session_incarnation;
      snapshot_.lifecycle = ReceiverSupervisorLifecycle::kConnected;
      snapshot_.connected = true;
      snapshot_.runtime_state.reset();
      snapshot_.session_metrics = session.metrics();
      snapshot_.runner_metrics = runner.metrics();
      snapshot_.last_terminal_error.clear();
    }
    auto link = std::make_shared<ReceiverLink>();
    link->transport = transport;
    link->incarnation = incarnation;
    {
      std::lock_guard<std::mutex> lock(correction_mutex_);
      active_ = link;
    }

    while (!stopping_ && runner.StepOnce())
      Update(session, runner);
    Update(session, runner);
    if (stopping_)
      break;

    static_cast<universal_gnss_transport::ByteSource&>(*transport).Close();
    {
      std::lock_guard<std::mutex> lock(correction_mutex_);
      if (active_ == link)
      {
        link->writer.Abandon();
        active_.reset();
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot_.lifecycle = ReceiverSupervisorLifecycle::kReconnecting;
      snapshot_.connected = false;
      snapshot_.runtime_state.reset();
      snapshot_.last_terminal_error = DescribeTerminalRead(runner.metrics());
      ++snapshot_.reconnect_attempt_count;
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
      snapshot_.forwarding_active = false;
      snapshot_.rtcm_forward_queue_depth = 0u;
#endif
    }
    if (!Wait(delay))
      break;
    delay = std::min(config_.maximum_reconnect_backoff, delay * 2);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.lifecycle = ReceiverSupervisorLifecycle::kStopped;
  snapshot_.connected = false;
  snapshot_.runtime_state.reset();
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  snapshot_.forwarding_active = false;
  snapshot_.rtcm_forward_queue_depth = 0u;
#endif
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

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
void ReceiverSupervisor::RunNtrip()
{
  const NtripSupervisorConfig& config = *config_.ntrip;
  universal_gnss_transport::TcpClientConfig tcp = config.tcp;
  if (!config.ntrip.tls_enabled)
  {
    tcp.nonblocking = true;
  }
  if (tcp.connect_timeout_ms == 0u)
  {
    tcp.connect_timeout_ms = 1000u;
  }
  if (tcp.read_timeout_ms == 0u && config.ntrip.tls_enabled)
  {
    tcp.read_timeout_ms = 100u;
  }
  universal_gnss_ntrip::NtripClient client(config.ntrip, tcp);
  std::vector<std::uint8_t> read_buffer(std::max<std::size_t>(1u, config.read_chunk_size));
  std::uint64_t gga_incarnation = 0u;
  std::size_t gga_position_observations = 0u;

  while (!stopping_)
  {
    const auto now_ns = SteadyNowNs();
    if (!client.IsConnected())
    {
      const auto& reconnect = client.reconnect_state();
      if (client.state() == universal_gnss_ntrip::NtripClientState::kFailed &&
          !reconnect.next_attempt_time_ns.has_value())
      {
        // NtripClient deliberately does not schedule configuration failures.
        // Do not turn that terminal policy decision into a supervisor retry loop.
        UpdateNtrip(client);
        if (!Wait(config.idle_poll_interval))
        {
          break;
        }
        continue;
      }
      if (reconnect.next_attempt_time_ns.has_value() && *reconnect.next_attempt_time_ns > now_ns)
      {
        const auto remaining_ns = *reconnect.next_attempt_time_ns - now_ns;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::nanoseconds(remaining_ns));
        if (!Wait(std::max(config.idle_poll_interval, remaining)))
        {
          break;
        }
        continue;
      }

      const universal_gnss_ntrip::NtripClientError connect_error =
          config.socket_factory ? client.AdoptConnectedSocket(config.socket_factory())
                                : client.Connect(now_ns);
      if (connect_error == universal_gnss_ntrip::NtripClientError::kNone)
      {
        (void)client.SendRequest(now_ns);
      }
      UpdateNtrip(client);
      if (!client.IsConnected())
      {
        if (!Wait(config.idle_poll_interval))
        {
          break;
        }
        continue;
      }
    }

    const auto receiver = Snapshot();
    if (receiver.session_incarnation != gga_incarnation)
    {
      gga_incarnation = receiver.session_incarnation;
      gga_position_observations = 0u;
    }
    const std::size_t position_observations =
        receiver.session_metrics.has_value() ? receiver.session_metrics->position_observations : 0u;
    if (client.state() == universal_gnss_ntrip::NtripClientState::kStreaming &&
        receiver.runtime_state.has_value() && position_observations > gga_position_observations)
    {
      gga_position_observations = position_observations;
      (void)client.MaybeInjectGga(*receiver.runtime_state, now_ns);
      UpdateNtrip(client);
      if (!client.IsConnected())
      {
        continue;
      }
    }

    FlushPendingRtcm();

    std::vector<universal_gnss_protocols::RtcmFrame> frames;
    const auto read = client.Read(read_buffer.data(), read_buffer.size(), now_ns, &frames);
    for (const auto& frame : frames)
    {
      ForwardRtcm(frame);
    }
    UpdateNtrip(client);
    if (read.client_error != universal_gnss_ntrip::NtripClientError::kNone)
    {
      continue;
    }
    if (read.bytes_read == 0u && !Wait(config.idle_poll_interval))
    {
      break;
    }
  }

  client.Disconnect();
  UpdateNtrip(client);
}

void ReceiverSupervisor::UpdateNtrip(const universal_gnss_ntrip::NtripClient& client)
{
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.ntrip_enabled = true;
  snapshot_.ntrip_state = client.state();
  snapshot_.ntrip_metrics = client.metrics();
  snapshot_.ntrip_correction_flow = client.correction_flow_state();
  snapshot_.ntrip_correction_health = client.BuildCorrectionHealth({});
  snapshot_.ntrip_reconnect = client.reconnect_state();
  snapshot_.ntrip_last_error = ToString(client.metrics().last_error);
}

void ReceiverSupervisor::ForwardRtcm(const universal_gnss_protocols::RtcmFrame& frame)
{
  std::size_t queue_depth = 0u;
  std::uint64_t bytes = 0u;
  std::uint64_t frames = 0u;
  bool overflow = false;
  bool active = false;
  {
    std::lock_guard<std::mutex> lock(correction_mutex_);
    if (!active_ || !static_cast<universal_gnss_transport::ByteSink&>(*active_->transport).IsOpen())
    {
      return;
    }
    active = true;
    if (!active_->writer.Enqueue({frame.raw_bytes, frame.message_type}))
    {
      overflow = true;
      queue_depth = active_->writer.size();
    } else
    {
      const auto outcome = active_->writer.Flush(*active_->transport);
      bytes = outcome.bytes_written;
      frames = outcome.frames_written;
      queue_depth = active_->writer.size();
      if (outcome.result == universal_gnss_transport::RtcmFrameWriter::FlushResult::kFailed)
      {
        static_cast<universal_gnss_transport::ByteSource&>(*active_->transport).Close();
      }
    }
  }
  if (!active)
  {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.rtcm_forward_queue_depth = queue_depth;
  if (overflow)
  {
    ++snapshot_.rtcm_forward_queue_overflows;
    return;
  }
  snapshot_.rtcm_forwarded_bytes += bytes;
  snapshot_.rtcm_forwarded_frames += frames;
  if (frames != 0u)
  {
    snapshot_.forwarding_active = true;
  }
}

void ReceiverSupervisor::FlushPendingRtcm()
{
  std::size_t queue_depth = 0u;
  std::uint64_t bytes = 0u;
  std::uint64_t frames = 0u;
  {
    std::lock_guard<std::mutex> lock(correction_mutex_);
    if (!active_ || active_->writer.empty() ||
        !static_cast<universal_gnss_transport::ByteSink&>(*active_->transport).IsOpen())
    {
      return;
    }
    const auto outcome = active_->writer.Flush(*active_->transport);
    bytes = outcome.bytes_written;
    frames = outcome.frames_written;
    queue_depth = active_->writer.size();
    if (outcome.result == universal_gnss_transport::RtcmFrameWriter::FlushResult::kFailed)
    {
      static_cast<universal_gnss_transport::ByteSource&>(*active_->transport).Close();
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.rtcm_forward_queue_depth = queue_depth;
  snapshot_.rtcm_forwarded_bytes += bytes;
  snapshot_.rtcm_forwarded_frames += frames;
  if (frames != 0u)
  {
    snapshot_.forwarding_active = true;
  }
}
#endif

bool ReceiverSupervisor::Wait(const std::chrono::milliseconds delay)
{
  std::unique_lock<std::mutex> lock(mutex_);
  return !condition_.wait_for(lock, delay, [this] { return stopping_.load(); });
}

} // namespace universal_gnss_runtime
