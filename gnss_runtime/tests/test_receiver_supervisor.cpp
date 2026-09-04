#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
#include <sys/socket.h>
#include <unistd.h>

#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#endif

#include "universal_gnss_runtime/receiver_supervisor.hpp"

namespace {

using universal_gnss_runtime::ReceiverSupervisor;
using universal_gnss_runtime::ReceiverSupervisorConfig;
using universal_gnss_runtime::ReceiverSupervisorLifecycle;
using universal_gnss_runtime::TransportFactoryResult;
using universal_gnss_transport::ByteDuplex;
using universal_gnss_transport::ReadResult;
using universal_gnss_transport::TransportError;
using universal_gnss_transport::TransportStatus;
using universal_gnss_transport::WriteResult;

struct TestContext
{
  int failures{0};

  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

struct FakeTransportWriteRecord
{
  mutable std::mutex mutex;
  std::vector<std::uint8_t> bytes;

  std::vector<std::uint8_t> Snapshot() const
  {
    std::lock_guard<std::mutex> lock(mutex);
    return bytes;
  }
};

class FakeTransport final : public ByteDuplex
{
public:
  explicit FakeTransport(std::vector<ReadResult> results, std::vector<std::uint8_t> bytes = {},
                         std::shared_ptr<FakeTransportWriteRecord> write_record = {})
      : results_(std::move(results)), bytes_(std::move(bytes)),
        write_record_(std::move(write_record))
  {
  }

  ReadResult Read(std::uint8_t* destination, const std::size_t capacity) override
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (result_index_ < results_.size())
    {
      const ReadResult result = results_[result_index_++];
      if (result.status == TransportStatus::kOk && result.bytes_read != 0u)
      {
        const std::size_t count = std::min({result.bytes_read, capacity, bytes_.size()});
        std::copy_n(bytes_.begin(), count, destination);
        return ReadResult{count, TransportStatus::kOk, TransportError::kNone};
      }
      return result;
    }
    condition_.wait(lock, [this] { return !open_; });
    return ReadResult{0u, TransportStatus::kClosed, TransportError::kClosed};
  }

  WriteResult Write(const std::uint8_t* data, const std::size_t size) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const WriteResult result = write_index_ < write_results_.size()
                                   ? write_results_[write_index_++]
                                   : WriteResult{size, TransportStatus::kOk, TransportError::kNone};
    const std::size_t accepted = std::min(size, result.bytes_written);
    written_.insert(written_.end(), data, data + static_cast<std::ptrdiff_t>(accepted));
    if (write_record_)
    {
      std::lock_guard<std::mutex> write_record_lock(write_record_->mutex);
      write_record_->bytes.insert(write_record_->bytes.end(), data,
                                  data + static_cast<std::ptrdiff_t>(accepted));
    }
    return result;
  }

  void SetWriteResults(std::vector<WriteResult> results)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    write_results_ = std::move(results);
    write_index_ = 0u;
  }

  std::vector<std::uint8_t> written() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return written_;
  }

  bool IsOpen() const override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
  }

  void Close() override
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      open_ = false;
    }
    condition_.notify_all();
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  bool open_{true};
  std::vector<ReadResult> results_;
  std::vector<std::uint8_t> bytes_;
  std::vector<WriteResult> write_results_;
  std::vector<std::uint8_t> written_;
  std::shared_ptr<FakeTransportWriteRecord> write_record_;
  std::size_t result_index_{0u};
  std::size_t write_index_{0u};
};

bool WaitFor(const std::function<bool()>& predicate,
             const std::chrono::milliseconds timeout = std::chrono::milliseconds(1000))
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (predicate())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}

ReceiverSupervisorConfig BaseConfig()
{
  ReceiverSupervisorConfig config;
  config.session.kind = universal_gnss_driver::ReceiverSessionKind::kNmea;
  config.initial_reconnect_backoff = std::chrono::milliseconds(30);
  config.maximum_reconnect_backoff = std::chrono::milliseconds(80);
  return config;
}

std::vector<std::uint8_t> ValidGga()
{
  const std::string sentence =
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
  return {sentence.begin(), sentence.end()};
}

void TestInitialConnectionAndRuntimeSemantics(TestContext& ctx)
{
  auto config = BaseConfig();
  config.transport_factory = [] {
    return TransportFactoryResult{
        std::make_unique<FakeTransport>(
            std::vector<ReadResult>{
                {ValidGga().size(), TransportStatus::kOk, TransportError::kNone}},
            ValidGga()),
        {}};
  };
  ReceiverSupervisor supervisor(std::move(config));
  ctx.Expect(supervisor.Start(), "supervisor should start with an injected transport factory");
  ctx.Expect(WaitFor([&] {
               const auto snapshot = supervisor.Snapshot();
               return snapshot.connected && snapshot.session_incarnation == 1u &&
                      snapshot.runtime_state.has_value();
             }),
             "initial successful connection should expose the existing session runtime state");
  const auto snapshot = supervisor.Snapshot();
  ctx.Expect(snapshot.runtime_state->fix_valid && snapshot.runtime_state->latitude_deg.has_value(),
             "supervisor must preserve the driver's parsed runtime semantics");
  supervisor.Stop();
}

void TestTerminalFailureReconnectsWithNewIncarnation(TestContext& ctx)
{
  std::size_t factory_calls = 0u;
  auto config = BaseConfig();
  config.transport_factory = [&factory_calls] {
    ++factory_calls;
    if (factory_calls == 1u)
    {
      return TransportFactoryResult{
          std::make_unique<FakeTransport>(
              std::vector<ReadResult>{{0u, TransportStatus::kError, TransportError::kReadFailure}}),
          {}};
    }
    return TransportFactoryResult{std::make_unique<FakeTransport>(std::vector<ReadResult>{}), {}};
  };
  ReceiverSupervisor supervisor(std::move(config));
  ctx.Expect(supervisor.Start(), "terminal-failure test supervisor should start");
  ctx.Expect(WaitFor([&] {
               const auto snapshot = supervisor.Snapshot();
               return snapshot.connected && snapshot.session_incarnation == 2u;
             }),
             "terminal read failure should lead to a successful new session incarnation");
  const auto snapshot = supervisor.Snapshot();
  ctx.Expect(snapshot.reconnect_attempt_count >= 1u && !snapshot.runtime_state.has_value(),
             "a reopened empty session must not present old incarnation state as fresh");
  supervisor.Stop();
}

void TestBackoffIsBoundedAndStopCancelsIt(TestContext& ctx)
{
  std::mutex times_mutex;
  std::vector<std::chrono::steady_clock::time_point> attempts;
  auto config = BaseConfig();
  config.initial_reconnect_backoff = std::chrono::milliseconds(25);
  config.maximum_reconnect_backoff = std::chrono::milliseconds(50);
  config.transport_factory = [&] {
    std::lock_guard<std::mutex> lock(times_mutex);
    attempts.push_back(std::chrono::steady_clock::now());
    return TransportFactoryResult{nullptr,
                                  std::string("open:") + std::string(1u, '\x01') + "failure"};
  };
  ReceiverSupervisor supervisor(std::move(config));
  ctx.Expect(supervisor.Start(), "backoff test supervisor should start");
  ctx.Expect(WaitFor([&] {
               std::lock_guard<std::mutex> lock(times_mutex);
               return attempts.size() >= 3u;
             }),
             "failed opens should retry");
  {
    std::lock_guard<std::mutex> lock(times_mutex);
    ctx.Expect(
        attempts[1] - attempts[0] >= std::chrono::milliseconds(20) &&
            attempts[2] - attempts[1] >= std::chrono::milliseconds(42),
        "reconnect attempts should use exponential bounded backoff rather than busy-looping");
  }
  const auto reconnecting = supervisor.Snapshot();
  ctx.Expect(reconnecting.last_terminal_error == "open:failed",
             "factory errors should be redacted before exposure");
  supervisor.Stop();
  std::size_t calls_after_stop = 0u;
  {
    std::lock_guard<std::mutex> lock(times_mutex);
    calls_after_stop = attempts.size();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  std::lock_guard<std::mutex> lock(times_mutex);
  ctx.Expect(attempts.size() == calls_after_stop,
             "clean stop must prevent reconnect attempts after it returns");
}

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
class SocketPair
{
public:
  ~SocketPair()
  {
    if (client_ >= 0)
      ::close(client_);
    if (peer_ >= 0)
      ::close(peer_);
  }

  bool Open()
  {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
      return false;
    client_ = fds[0];
    peer_ = fds[1];
    return true;
  }

  int ReleaseClient()
  {
    const int fd = client_;
    client_ = -1;
    return fd;
  }

  bool Write(const std::vector<std::uint8_t>& bytes)
  {
    return ::write(peer_, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size());
  }

  std::vector<std::uint8_t> ReadAvailable()
  {
    std::vector<std::uint8_t> bytes;
    std::uint8_t buffer[512];
    for (;;)
    {
      const ssize_t count = ::recv(peer_, buffer, sizeof(buffer), MSG_DONTWAIT);
      if (count <= 0)
        return bytes;
      bytes.insert(bytes.end(), buffer, buffer + count);
    }
  }

  void ClosePeer()
  {
    if (peer_ >= 0)
    {
      ::close(peer_);
      peer_ = -1;
    }
  }

private:
  int client_{-1};
  int peer_{-1};
};

std::vector<std::uint8_t> Rtcm(const std::uint16_t type)
{
  std::vector<std::uint8_t> bytes = {0xD3u, 0u, 2u, static_cast<std::uint8_t>(type >> 4u),
                                     static_cast<std::uint8_t>((type & 0x0Fu) << 4u)};
  const std::uint32_t crc = universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  bytes.push_back(static_cast<std::uint8_t>(crc >> 16u));
  bytes.push_back(static_cast<std::uint8_t>(crc >> 8u));
  bytes.push_back(static_cast<std::uint8_t>(crc));
  return bytes;
}

std::vector<std::uint8_t> AcceptedResponse(const std::vector<std::uint8_t>& payload)
{
  const std::string header = "ICY 200 OK\r\n\r\n";
  std::vector<std::uint8_t> response(header.begin(), header.end());
  response.insert(response.end(), payload.begin(), payload.end());
  return response;
}

std::size_t CountText(const std::vector<std::uint8_t>& bytes, const std::string& text)
{
  const std::string captured(bytes.begin(), bytes.end());
  std::size_t count = 0u;
  std::size_t position = 0u;
  while ((position = captured.find(text, position)) != std::string::npos)
  {
    ++count;
    position += text.size();
  }
  return count;
}

std::vector<std::uint8_t> InvalidGga()
{
  const std::string sentence =
      "$GPGGA,123519,4807.038,N,01131.000,E,0,08,0.9,545.4,M,46.9,M,,*46\r\n";
  return {sentence.begin(), sentence.end()};
}

void TestGgaUsesFreshAuthoritativePosition(TestContext& ctx)
{
  for (const bool valid_fix : {true, false})
  {
    SocketPair ntrip_socket;
    ctx.Expect(ntrip_socket.Open(), "GGA fixture socketpair should open");
    ctx.Expect(ntrip_socket.Write(AcceptedResponse({})),
               "GGA fixture caster should accept a response");
    int ntrip_fd = ntrip_socket.ReleaseClient();
    auto config = BaseConfig();
    const auto receiver_bytes = valid_fix ? ValidGga() : InvalidGga();
    config.transport_factory = [receiver_bytes] {
      return TransportFactoryResult{
          std::make_unique<FakeTransport>(
              std::vector<ReadResult>{
                  {receiver_bytes.size(), TransportStatus::kOk, TransportError::kNone}},
              receiver_bytes),
          {}};
    };
    universal_gnss_runtime::NtripSupervisorConfig ntrip;
    ntrip.ntrip.host = "test";
    ntrip.ntrip.mountpoint = "RTCM";
    ntrip.ntrip.send_gga = true;
    ntrip.idle_poll_interval = std::chrono::milliseconds(2);
    ntrip.socket_factory = [ntrip_fd]() mutable {
      const int result = ntrip_fd;
      ntrip_fd = -1;
      return result;
    };
    config.ntrip = std::move(ntrip);
    ReceiverSupervisor supervisor(std::move(config));
    ctx.Expect(supervisor.Start(), "GGA-policy supervisor should start");

    std::vector<std::uint8_t> caster_bytes;
    ctx.Expect(WaitFor([&] {
                 const auto bytes = ntrip_socket.ReadAvailable();
                 caster_bytes.insert(caster_bytes.end(), bytes.begin(), bytes.end());
                 const auto snapshot = supervisor.Snapshot();
                 return snapshot.ntrip_correction_flow.response_accepted &&
                        snapshot.runtime_state.has_value();
               }),
               "GGA fixture should reach accepted NTRIP response and a receiver runtime state");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto trailing_bytes = ntrip_socket.ReadAvailable();
    caster_bytes.insert(caster_bytes.end(), trailing_bytes.begin(), trailing_bytes.end());
    const std::size_t gga_count = CountText(caster_bytes, "$GPGGA,");
    ctx.Expect(
        valid_fix ? gga_count == 1u : gga_count == 0u,
        valid_fix
            ? "one fresh valid position should inject exactly one GGA despite cached snapshots"
            : "an invalid receiver fix must not fabricate a GGA sentence");
    supervisor.Stop();
  }
}

void TestNtripStopAndRedaction(TestContext& ctx)
{
  {
    SocketPair ntrip_socket;
    ctx.Expect(ntrip_socket.Open(), "NTRIP stop fixture socketpair should open");
    ctx.Expect(ntrip_socket.Write(AcceptedResponse({})),
               "NTRIP stop fixture caster should accept a response");
    int ntrip_fd = ntrip_socket.ReleaseClient();
    std::atomic<std::size_t> factory_calls{0u};
    auto config = BaseConfig();
    config.transport_factory = [] {
      return TransportFactoryResult{std::make_unique<FakeTransport>(std::vector<ReadResult>{}), {}};
    };
    universal_gnss_runtime::NtripSupervisorConfig ntrip;
    ntrip.ntrip.host = "test";
    ntrip.ntrip.mountpoint = "RTCM";
    ntrip.ntrip.reconnect_policy.initial_delay_ms = 200u;
    ntrip.ntrip.reconnect_policy.max_delay_ms = 200u;
    ntrip.socket_factory = [&] {
      ++factory_calls;
      const int result = ntrip_fd;
      ntrip_fd = -1;
      return result;
    };
    config.ntrip = std::move(ntrip);
    ReceiverSupervisor supervisor(std::move(config));
    ctx.Expect(supervisor.Start(), "NTRIP stop supervisor should start");
    ctx.Expect(WaitFor([&] { return supervisor.Snapshot().ntrip_metrics.response_received; }),
               "NTRIP stop fixture should reach an accepted response");
    ntrip_socket.ClosePeer();
    ctx.Expect(WaitFor([&] { return supervisor.Snapshot().ntrip_metrics.reconnect_count == 1u; }),
               "NTRIP disconnection should schedule its own reconnect");
    supervisor.Stop();
    const std::size_t calls_after_stop = factory_calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    ctx.Expect(factory_calls.load() == calls_after_stop,
               "stop must cancel the pending NTRIP reconnect before another socket request");
  }

  {
    auto config = BaseConfig();
    config.transport_factory = [] {
      return TransportFactoryResult{std::make_unique<FakeTransport>(std::vector<ReadResult>{}), {}};
    };
    universal_gnss_runtime::NtripSupervisorConfig ntrip;
    ntrip.ntrip.host = "test";
    ntrip.ntrip.mountpoint = "RTCM";
    ntrip.ntrip.username = "operator";
    ntrip.ntrip.password = "secret-not-for-status";
    ntrip.socket_factory = [] { return -1; };
    config.ntrip = std::move(ntrip);
    ReceiverSupervisor supervisor(std::move(config));
    ctx.Expect(supervisor.Start(), "redaction supervisor should start");
    ctx.Expect(WaitFor([&] { return supervisor.Snapshot().ntrip_last_error == "configuration"; }),
               "invalid adopted descriptor should expose only the bounded configuration category");
    const auto snapshot = supervisor.Snapshot();
    ctx.Expect(snapshot.ntrip_last_error.find("operator") == std::string::npos &&
                   snapshot.ntrip_last_error.find("secret-not-for-status") == std::string::npos &&
                   snapshot.last_terminal_error.find("secret-not-for-status") == std::string::npos,
               "NTRIP credentials must not appear in supervisor snapshots or errors");
    supervisor.Stop();
  }
}

void TestNtripForwardingAndIndependentReconnects(TestContext& ctx)
{
  SocketPair first_ntrip;
  SocketPair second_ntrip;
  ctx.Expect(first_ntrip.Open() && second_ntrip.Open(), "NTRIP socketpair fixtures should open");
  const auto first_frame = Rtcm(1077u);
  const auto second_frame = Rtcm(1087u);
  ctx.Expect(first_ntrip.Write(AcceptedResponse(first_frame)),
             "first caster response should be writable");
  std::vector<int> ntrip_fds = {first_ntrip.ReleaseClient(), second_ntrip.ReleaseClient()};
  std::size_t ntrip_fd_index = 0u;
  FakeTransport* first_receiver = nullptr;
  FakeTransport* second_receiver = nullptr;
  const auto first_receiver_writes = std::make_shared<FakeTransportWriteRecord>();
  const auto second_receiver_writes = std::make_shared<FakeTransportWriteRecord>();
  std::size_t receiver_opens = 0u;
  auto config = BaseConfig();
  config.transport_factory = [&] {
    const auto write_record = receiver_opens == 0u ? first_receiver_writes : second_receiver_writes;
    auto transport = std::make_unique<FakeTransport>(std::vector<ReadResult>{},
                                                     std::vector<std::uint8_t>{}, write_record);
    if (receiver_opens++ == 0u)
    {
      first_receiver = transport.get();
      first_receiver->SetWriteResults({{3u, TransportStatus::kOk, TransportError::kNone},
                                       {0u, TransportStatus::kOk, TransportError::kNone}});
    } else
    {
      second_receiver = transport.get();
    }
    return TransportFactoryResult{std::move(transport), {}};
  };
  universal_gnss_runtime::NtripSupervisorConfig ntrip;
  ntrip.ntrip.host = "test";
  ntrip.ntrip.mountpoint = "RTCM";
  ntrip.ntrip.reconnect_policy.initial_delay_ms = 20u;
  ntrip.ntrip.reconnect_policy.max_delay_ms = 20u;
  ntrip.idle_poll_interval = std::chrono::milliseconds(2);
  ntrip.socket_factory = [&] {
    return ntrip_fd_index < ntrip_fds.size() ? ntrip_fds[ntrip_fd_index++] : -1;
  };
  config.ntrip = std::move(ntrip);
  ReceiverSupervisor supervisor(std::move(config));
  ctx.Expect(supervisor.Start(), "NTRIP-enabled supervisor should start");
  ctx.Expect(WaitFor([&] {
               return first_receiver != nullptr &&
                      supervisor.Snapshot().ntrip_metrics.response_received;
             }),
             "NtripClient should accept the first response without restarting the receiver");
  ctx.Expect(WaitFor([&] { return first_receiver_writes->Snapshot() == first_frame; }),
             "partial receiver writes should flush the complete RTCM frame in order");
  const auto forwarding_snapshot = supervisor.Snapshot();
  ctx.Expect(forwarding_snapshot.ntrip_enabled && forwarding_snapshot.ntrip_metrics.connected &&
                 forwarding_snapshot.ntrip_correction_flow.response_accepted &&
                 forwarding_snapshot.ntrip_correction_flow.valid_rtcm_frames == 1u &&
                 forwarding_snapshot.forwarding_active &&
                 forwarding_snapshot.rtcm_forward_queue_depth == 0u &&
                 forwarding_snapshot.rtcm_forwarded_frames == 1u &&
                 forwarding_snapshot.session_incarnation == 1u,
             "supervisor status must retain distinct NTRIP connection, response, flow, forwarding, "
             "queue, and incarnation state");
  first_receiver->Close();
  ctx.Expect(WaitFor([&] {
               return supervisor.Snapshot().session_incarnation == 2u && second_receiver != nullptr;
             }),
             "receiver reconnect should create a new incarnation without restarting NTRIP");
  const auto first_sink_after_replacement = first_receiver_writes->Snapshot();
  ctx.Expect(first_sink_after_replacement == first_frame,
             "replaced receiver transport must receive no old RTCM suffix");
  ctx.Expect(first_ntrip.Write(second_frame),
             "healthy NTRIP stream should accept another correction frame");
  ctx.Expect(WaitFor([&] { return second_receiver_writes->Snapshot() == second_frame; }),
             "forwarding should resume only on the new receiver incarnation");
  const std::uint64_t incarnation_before_ntrip_reconnect =
      supervisor.Snapshot().session_incarnation;
  first_ntrip.ClosePeer();
  ctx.Expect(second_ntrip.Write(AcceptedResponse(second_frame)),
             "second caster response should be writable");
  ctx.Expect(WaitFor([&] {
               const auto snapshot = supervisor.Snapshot();
               return snapshot.ntrip_metrics.reconnect_count >= 1u &&
                      snapshot.ntrip_metrics.response_received;
             }),
             "NTRIP reconnect should be independent of the healthy receiver");
  ctx.Expect(supervisor.Snapshot().session_incarnation == incarnation_before_ntrip_reconnect,
             "NTRIP reconnect must not restart the receiver");
  supervisor.Stop();
}
#endif

} // namespace

int main()
{
  TestContext ctx;
  TestInitialConnectionAndRuntimeSemantics(ctx);
  TestTerminalFailureReconnectsWithNewIncarnation(ctx);
  TestBackoffIsBoundedAndStopCancelsIt(ctx);
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  TestGgaUsesFreshAuthoritativePosition(ctx);
  TestNtripStopAndRedaction(ctx);
  TestNtripForwardingAndIndependentReconnects(ctx);
#endif
  return ctx.failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
