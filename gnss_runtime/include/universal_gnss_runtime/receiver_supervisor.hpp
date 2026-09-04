#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
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
#include "universal_gnss_ntrip/ntrip_client.hpp"
#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/rtcm_frame_writer.hpp"

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

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
// A test-only factory may supply an already-connected socket. Production
// callers leave it empty so NtripClient owns TCP connection semantics.
using NtripSocketFactory = std::function<int()>;

struct NtripSupervisorConfig
{
  universal_gnss_ntrip::NtripConfig ntrip{};
  universal_gnss_transport::TcpClientConfig tcp{};
  std::size_t read_chunk_size{4096u};
  std::chrono::milliseconds idle_poll_interval{10};
  NtripSocketFactory socket_factory{};
};
#endif

struct ReceiverSupervisorConfig
{
  universal_gnss_driver::ReceiverSessionConfig session{};
  universal_gnss_driver::ReceiverSessionRunnerConfig runner{};
  std::chrono::milliseconds initial_reconnect_backoff{100};
  std::chrono::milliseconds maximum_reconnect_backoff{5000};
  TransportFactory transport_factory{};
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  std::optional<NtripSupervisorConfig> ntrip{};
#endif
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
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  bool ntrip_enabled{false};
  universal_gnss_ntrip::NtripClientState ntrip_state{
      universal_gnss_ntrip::NtripClientState::kDisconnected};
  universal_gnss_ntrip::NtripConnectionMetrics ntrip_metrics{};
  universal_gnss_ntrip::NtripCorrectionFlowState ntrip_correction_flow{};
  universal_gnss::GnssHealthSummary ntrip_correction_health{};
  universal_gnss_ntrip::NtripReconnectState ntrip_reconnect{};
  bool forwarding_active{false};
  std::size_t rtcm_forward_queue_depth{0u};
  std::uint64_t rtcm_forward_queue_overflows{0u};
  std::uint64_t rtcm_forwarded_frames{0u};
  std::uint64_t rtcm_forwarded_bytes{0u};
  // NtripClientError name only; never connection text or configuration.
  std::string ntrip_last_error{};
#endif
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
  struct ReceiverLink;

  void Run();
  void Update(const universal_gnss_driver::ReceiverSession& session,
              const universal_gnss_driver::ReceiverSessionRunner& runner);
  bool Wait(std::chrono::milliseconds delay);
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  void RunNtrip();
  void UpdateNtrip(const universal_gnss_ntrip::NtripClient& client);
  void ForwardRtcm(const universal_gnss_protocols::RtcmFrame& frame);
  void FlushPendingRtcm();
#endif

  ReceiverSupervisorConfig config_;
  std::atomic<bool> stopping_{false};
  mutable std::mutex mutex_;
  ReceiverSupervisorSnapshot snapshot_{};
  std::shared_ptr<ReceiverLink> active_{};
  mutable std::mutex correction_mutex_;
  std::condition_variable condition_;
  std::thread worker_{};
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  std::thread ntrip_worker_{};
#endif
};

} // namespace universal_gnss_runtime
