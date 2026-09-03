#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_driver/receiver_session_runner.hpp"
#include "universal_gnss_transport/byte_stream.hpp"

namespace universal_gnss_runtime {

enum class ReceiverSupervisorLifecycle : std::uint8_t
{
  kStopped,
  kStarting,
  kConnected,
  kReconnecting,
  kStopping,
};

const char* ToString(ReceiverSupervisorLifecycle lifecycle);

struct TransportFactoryResult
{
  std::unique_ptr<universal_gnss_transport::ByteDuplex> transport{};
  // Optional private factory diagnostic; the supervisor never exposes it.
  std::string error{};
};

using TransportFactory = std::function<TransportFactoryResult()>;

struct ReceiverSupervisorConfig
{
  universal_gnss_driver::ReceiverSessionConfig session{};
  universal_gnss_driver::ReceiverSessionRunnerConfig runner{};
  std::chrono::milliseconds initial_reconnect_backoff{100};
  std::chrono::milliseconds maximum_reconnect_backoff{5000};
  TransportFactory transport_factory{};
};

struct ReceiverSupervisorSnapshot
{
  ReceiverSupervisorLifecycle lifecycle{ReceiverSupervisorLifecycle::kStopped};
  bool connected{false};
  std::uint64_t reconnect_attempt_count{0u};
  // This increments only after a transport was successfully opened and a new
  // ReceiverSession was constructed. State in this snapshot belongs to it.
  std::uint64_t session_incarnation{0u};
  std::optional<universal_gnss::GnssRuntimeState> runtime_state{};
  std::optional<universal_gnss_driver::ReceiverSessionMetrics> session_metrics{};
  std::optional<universal_gnss_driver::ReceiverSessionRunnerMetrics> runner_metrics{};
  // Bounded and sanitized, suitable for a status endpoint or log line.
  std::string last_terminal_error{};
};

class ReceiverSupervisor
{
public:
  explicit ReceiverSupervisor(ReceiverSupervisorConfig config);
  ~ReceiverSupervisor();

  ReceiverSupervisor(const ReceiverSupervisor&) = delete;
  ReceiverSupervisor& operator=(const ReceiverSupervisor&) = delete;

  bool Start();
  void Stop();
  ReceiverSupervisorSnapshot Snapshot() const;

private:
  void Run();
  void Update(const universal_gnss_driver::ReceiverSession& session,
              const universal_gnss_driver::ReceiverSessionRunner& runner);
  bool Wait(std::chrono::milliseconds delay);

  ReceiverSupervisorConfig config_;
  std::atomic<bool> stopping_{false};
  mutable std::mutex mutex_;
  ReceiverSupervisorSnapshot snapshot_{};
  std::shared_ptr<universal_gnss_transport::ByteDuplex> active_{};
  std::condition_variable condition_;
  std::thread worker_{};
};

} // namespace universal_gnss_runtime
