#include <algorithm>
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

class FakeTransport final : public ByteDuplex
{
public:
  explicit FakeTransport(std::vector<ReadResult> results, std::vector<std::uint8_t> bytes = {})
      : results_(std::move(results)), bytes_(std::move(bytes))
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

  WriteResult Write(const std::uint8_t*, std::size_t) override { return WriteResult{}; }

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
  std::size_t result_index_{0u};
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

} // namespace

int main()
{
  TestContext ctx;
  TestInitialConnectionAndRuntimeSemantics(ctx);
  TestTerminalFailureReconnectsWithNewIncarnation(ctx);
  TestBackoffIsBoundedAndStopCancelsIt(ctx);
  return ctx.failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
