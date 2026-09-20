#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "universal_gnss_runtime/posix_serial_factory.hpp"
#include "universal_gnss_runtime/receiver_supervisor.hpp"

namespace
{

using universal_gnss_runtime::ReceiverSupervisor;
using universal_gnss_runtime::ReceiverSupervisorConfig;
using universal_gnss_runtime::ReceiverSupervisorLifecycle;

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

class SilentPseudoTerminal
{
public:
  ~SilentPseudoTerminal()
  {
    CloseMaster();
  }

  void CloseMaster()
  {
    if (master_fd_ >= 0)
    {
      ::close(master_fd_);
      master_fd_ = -1;
    }
  }

  bool Open()
  {
    master_fd_ = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd_ < 0 || ::grantpt(master_fd_) != 0 || ::unlockpt(master_fd_) != 0)
    {
      return false;
    }
    char* slave_name = ::ptsname(master_fd_);
    if (slave_name == nullptr)
    {
      return false;
    }
    slave_path_ = slave_name;
    return true;
  }

  const std::string& slave_path() const
  {
    return slave_path_;
  }

  bool WriteMaster(const std::string& bytes)
  {
    std::size_t written = 0u;
    while (written < bytes.size())
    {
      const ssize_t count = ::write(master_fd_, bytes.data() + written, bytes.size() - written);
      if (count < 0 && errno == EINTR)
      {
        continue;
      }
      if (count <= 0)
      {
        return false;
      }
      written += static_cast<std::size_t>(count);
    }
    return true;
  }

private:
  int master_fd_{-1};
  std::string slave_path_{};
};

bool WaitFor(const std::function<bool()>& predicate,
             const std::chrono::milliseconds timeout = std::chrono::milliseconds(1000))
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (predicate())
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}

int CountOpenDescriptors()
{
  DIR* directory = ::opendir("/proc/self/fd");
  if (directory == nullptr)
  {
    return -1;
  }
  int count = 0;
  while (const dirent* entry = ::readdir(directory))
  {
    if (entry->d_name[0] != '.')
    {
      ++count;
    }
  }
  ::closedir(directory);
  return count;
}

ReceiverSupervisorConfig BaseConfig(const std::string& device_path,
                                    const std::uint32_t read_timeout_ms)
{
  ReceiverSupervisorConfig config;
  config.session.kind = universal_gnss_driver::ReceiverSessionKind::kNmea;
  config.idle_read_poll_interval = std::chrono::milliseconds(5);
  config.transport_factory = universal_gnss_runtime::MakePosixSerialTransportFactory(
      universal_gnss_transport::PosixSerialConfig{device_path, 115200u, false, read_timeout_ms});
  return config;
}

void TestSilentBlockingReceiverStopsPromptly(TestContext& ctx)
{
  constexpr int kIterations = 50;
  for (int iteration = 0; iteration < kIterations; ++iteration)
  {
    SilentPseudoTerminal pty;
    ctx.Expect(pty.Open(), "silent-PTY fixture should open for blocking shutdown regression");
    if (pty.slave_path().empty())
    {
      return;
    }

    const int open_descriptors_before = CountOpenDescriptors();
    ctx.Expect(open_descriptors_before > 0,
               "silent-PTY fixture should observe process descriptors");
    ReceiverSupervisor supervisor(BaseConfig(pty.slave_path(), 0u));
    ctx.Expect(supervisor.Start(), "silent blocking-PTY supervisor should start");
    ctx.Expect(WaitFor([&] { return supervisor.Snapshot().connected; }),
               "silent blocking-PTY receiver should connect before shutdown");

    // The peer remains open and sends no bytes. This lets the real transport
    // enter its indefinite, cancellation-aware receive wait.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto stop_started = std::chrono::steady_clock::now();
    supervisor.Stop();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
    ctx.Expect(stop_elapsed < std::chrono::milliseconds(250),
               "silent blocking-PTY shutdown must be bounded without injected traffic");
    const auto stopped = supervisor.Snapshot();
    ctx.Expect(stopped.lifecycle == ReceiverSupervisorLifecycle::kStopped && !stopped.connected,
               "silent blocking-PTY shutdown must leave no connected worker");
    ctx.Expect(stopped.runner_metrics.has_value() && stopped.runner_metrics->read_errors == 0u &&
                   stopped.last_terminal_error.empty(),
               "normal serial cancellation must not surface as a receiver read failure");
    ctx.Expect(CountOpenDescriptors() == open_descriptors_before,
               "stopped supervisor must release the serial fd and its wake pipe");
  }
}

void TestIdleTimeoutDoesNotReconnect(TestContext& ctx)
{
  SilentPseudoTerminal pty;
  ctx.Expect(pty.Open(), "silent-PTY fixture should open for idle regression");
  if (pty.slave_path().empty())
  {
    return;
  }

  ReceiverSupervisor supervisor(BaseConfig(pty.slave_path(), 20u));
  ctx.Expect(supervisor.Start(), "idle-PTY supervisor should start");
  ctx.Expect(WaitFor([&] { return supervisor.Snapshot().connected; }),
             "idle-PTY receiver should establish a session before any bytes arrive");

  const std::string gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
  const auto split = gga.size() / 2u;
  ctx.Expect(pty.WriteMaster(gga.substr(0u, split)),
             "idle-PTY fixture should accept the first sentence fragment");
  const auto connected = supervisor.Snapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  const auto idle = supervisor.Snapshot();
  ctx.Expect(idle.connected && idle.session_incarnation == connected.session_incarnation &&
                 idle.reconnect_attempt_count == connected.reconnect_attempt_count &&
                 !idle.runtime_state.has_value(),
             "normal idle reads must retain the same receiver session without reconnect");

  ctx.Expect(pty.WriteMaster(gga.substr(split)),
             "idle-PTY fixture should accept the remaining sentence fragment");
  ctx.Expect(WaitFor([&] {
               const auto snapshot = supervisor.Snapshot();
               return snapshot.runtime_state.has_value() && snapshot.runtime_state->fix_valid;
             }),
             "parser state must survive idle reads between fragments of one GGA sentence");
  const auto observed = supervisor.Snapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  const auto retained = supervisor.Snapshot();
  ctx.Expect(retained.connected && retained.session_incarnation == connected.session_incarnation &&
                 retained.reconnect_attempt_count == connected.reconnect_attempt_count &&
                 retained.runtime_state.has_value() && observed.runtime_state.has_value() &&
                 retained.runtime_state->timestamp_ns == observed.runtime_state->timestamp_ns &&
                 retained.session_metrics.has_value() && observed.session_metrics.has_value() &&
                 retained.session_metrics->runtime_updates ==
                     observed.session_metrics->runtime_updates,
             "idle reads must preserve the last physical observation and runtime state");

  const auto stop_started = std::chrono::steady_clock::now();
  supervisor.Stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
  ctx.Expect(stop_elapsed < std::chrono::milliseconds(250),
             "idle-PTY shutdown must remain bounded without serial traffic");
}

void TestPeerHangupStillDisconnects(TestContext& ctx)
{
  SilentPseudoTerminal pty;
  ctx.Expect(pty.Open(), "pseudo-terminal fixture should open for disconnect regression");
  if (pty.slave_path().empty())
  {
    return;
  }

  ReceiverSupervisor supervisor(BaseConfig(pty.slave_path(), 20u));
  ctx.Expect(supervisor.Start(), "disconnect regression supervisor should start");
  ctx.Expect(WaitFor([&] { return supervisor.Snapshot().connected; }),
             "disconnect regression receiver should connect before peer loss");
  const auto before = supervisor.Snapshot();

  pty.CloseMaster();
  ctx.Expect(WaitFor([&] {
               const auto snapshot = supervisor.Snapshot();
               return !snapshot.connected &&
                      snapshot.reconnect_attempt_count > before.reconnect_attempt_count;
             }),
             "actual peer hangup must remain terminal and schedule reconnect");
  supervisor.Stop();
}

}  // namespace

int main()
{
  TestContext ctx;
  TestSilentBlockingReceiverStopsPromptly(ctx);
  TestIdleTimeoutDoesNotReconnect(ctx);
  TestPeerHangupStillDisconnects(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All POSIX receiver supervisor tests passed\n";
  return EXIT_SUCCESS;
}
