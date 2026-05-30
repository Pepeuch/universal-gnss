#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_ntrip/ntrip_config.hpp"
#include "universal_gnss_ntrip/ntrip_reconnect_policy.hpp"

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)

#include <cerrno>

#include <sys/socket.h>
#include <unistd.h>

#include "universal_gnss_ntrip/ntrip_client.hpp"

#endif

namespace
{

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

void TestFirstFailureSchedulesInitialDelay(TestContext& ctx)
{
  universal_gnss_ntrip::NtripReconnectPolicy policy;
  policy.initial_delay_ms = 1000u;
  policy.max_delay_ms = 5000u;
  policy.multiplier = 2.0;

  universal_gnss_ntrip::NtripReconnectState state;
  const auto decision = policy.OnFailure(state, 1000000000LL);

  ctx.Expect(decision.scheduled,
             "the first reconnect failure should schedule a retry");
  ctx.Expect(decision.attempt_count == 1u &&
                 state.attempt_count == 1u &&
                 state.current_delay_ms == 1000u,
             "the first scheduled retry should use the initial reconnect delay");
  ctx.Expect(state.last_failure_time_ns == 1000000000LL &&
                 state.next_attempt_time_ns == 2000000000LL,
             "the reconnect state should store the failure time and first retry deadline");
  ctx.Expect(!policy.ShouldReconnect(state, 1999999999LL) &&
                 policy.ShouldReconnect(state, 2000000000LL),
             "ShouldReconnect should flip once the scheduled retry time is reached");
}

void TestExponentialBackoffAndMaxDelay(TestContext& ctx)
{
  universal_gnss_ntrip::NtripReconnectPolicy policy;
  policy.initial_delay_ms = 100u;
  policy.max_delay_ms = 250u;
  policy.multiplier = 3.0;

  universal_gnss_ntrip::NtripReconnectState state;
  policy.OnFailure(state, 0LL);
  ctx.Expect(state.current_delay_ms == 100u,
             "the first reconnect delay should use the configured initial delay");

  policy.OnFailure(state, 100000000LL);
  ctx.Expect(state.current_delay_ms == 250u,
             "the second reconnect delay should grow and respect the configured cap");

  policy.OnFailure(state, 200000000LL);
  ctx.Expect(state.current_delay_ms == 250u && state.attempt_count == 3u,
             "subsequent reconnect delays should remain capped at the max delay");
}

void TestMaxAttemptsStopsScheduling(TestContext& ctx)
{
  universal_gnss_ntrip::NtripReconnectPolicy policy;
  policy.initial_delay_ms = 500u;
  policy.max_delay_ms = 5000u;
  policy.multiplier = 2.0;
  policy.max_attempts = 2u;

  universal_gnss_ntrip::NtripReconnectState state;
  const auto first = policy.OnFailure(state, 1000000000LL);
  const auto second = policy.OnFailure(state, 2000000000LL);
  const auto third = policy.OnFailure(state, 3000000000LL);

  ctx.Expect(first.scheduled && second.scheduled,
             "reconnect scheduling should continue until the max attempt count is reached");
  ctx.Expect(!third.scheduled &&
                 !third.can_attempt &&
                 state.attempt_count == 2u &&
                 !state.next_attempt_time_ns.has_value(),
             "max_attempts should stop scheduling additional reconnect retries");
}

void TestSuccessResetBehavior(TestContext& ctx)
{
  universal_gnss_ntrip::NtripReconnectPolicy policy;
  policy.initial_delay_ms = 250u;
  policy.max_delay_ms = 2000u;
  policy.multiplier = 2.0;
  policy.reset_after_success = true;

  universal_gnss_ntrip::NtripReconnectState state;
  policy.OnFailure(state, 1000000000LL);
  policy.OnFailure(state, 2000000000LL);
  policy.OnSuccess(state, 3000000000LL);

  ctx.Expect(state.attempt_count == 0u &&
                 state.current_delay_ms == 0u &&
                 !state.next_attempt_time_ns.has_value() &&
                 !state.last_failure_time_ns.has_value(),
             "successful reconnects should reset backoff state when reset_after_success is enabled");
  ctx.Expect(state.last_success_time_ns == 3000000000LL,
             "successful reconnects should store the last success timestamp");
}

void TestSuccessKeepsStateWhenResetDisabled(TestContext& ctx)
{
  universal_gnss_ntrip::NtripReconnectPolicy policy;
  policy.initial_delay_ms = 250u;
  policy.max_delay_ms = 2000u;
  policy.multiplier = 2.0;
  policy.reset_after_success = false;

  universal_gnss_ntrip::NtripReconnectState state;
  policy.OnFailure(state, 1000000000LL);
  policy.OnSuccess(state, 3000000000LL);

  ctx.Expect(state.attempt_count == 1u &&
                 state.current_delay_ms == 250u &&
                 !state.next_attempt_time_ns.has_value(),
             "success without reset should preserve the current backoff while clearing the pending deadline");
  ctx.Expect(state.last_failure_time_ns == 1000000000LL &&
                 state.last_success_time_ns == 3000000000LL,
             "success without reset should preserve failure history and store the success time");
}

void TestDisabledPolicyNeverReconnects(TestContext& ctx)
{
  universal_gnss_ntrip::NtripReconnectPolicy policy;
  policy.enabled = false;

  universal_gnss_ntrip::NtripReconnectState state;
  const auto decision = policy.OnFailure(state, 1000000000LL);

  ctx.Expect(!decision.scheduled &&
                 !policy.CanAttempt(state) &&
                 !policy.ShouldReconnect(state, 2000000000LL),
             "disabled reconnect policies should never schedule automatic retries");
  ctx.Expect(state.attempt_count == 0u &&
                 !state.next_attempt_time_ns.has_value(),
             "disabled reconnect policies should leave the reconnect schedule empty");
}

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)

class SocketPair
{
public:
  SocketPair() = default;

  ~SocketPair()
  {
    ClosePeer();
    CloseClient();
  }

  bool Open()
  {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
    {
      return false;
    }

    client_fd_ = fds[0];
    peer_fd_ = fds[1];
    return true;
  }

  int ReleaseClientFd()
  {
    const int fd = client_fd_;
    client_fd_ = -1;
    return fd;
  }

  bool WritePeer(const std::string& text)
  {
    const auto* bytes =
        reinterpret_cast<const std::uint8_t*>(text.data());
    std::size_t offset = 0u;
    while (offset < text.size())
    {
      const ssize_t bytes_written =
          ::write(peer_fd_,
                  bytes + static_cast<std::ptrdiff_t>(offset),
                  text.size() - offset);
      if (bytes_written < 0)
      {
        if (errno == EINTR)
        {
          continue;
        }
        return false;
      }

      offset += static_cast<std::size_t>(bytes_written);
    }

    return true;
  }

  std::vector<std::uint8_t> ReadPeerExact(const std::size_t size)
  {
    std::vector<std::uint8_t> buffer(size, 0u);
    std::size_t offset = 0u;
    while (offset < size)
    {
      const ssize_t bytes_read =
          ::read(peer_fd_,
                 buffer.data() + static_cast<std::ptrdiff_t>(offset),
                 size - offset);
      if (bytes_read < 0)
      {
        if (errno == EINTR)
        {
          continue;
        }
        buffer.resize(offset);
        break;
      }

      if (bytes_read == 0)
      {
        buffer.resize(offset);
        break;
      }

      offset += static_cast<std::size_t>(bytes_read);
    }

    return buffer;
  }

  void ClosePeer()
  {
    if (peer_fd_ >= 0)
    {
      ::close(peer_fd_);
      peer_fd_ = -1;
    }
  }

  void CloseClient()
  {
    if (client_fd_ >= 0)
    {
      ::close(client_fd_);
      client_fd_ = -1;
    }
  }

private:
  int client_fd_{-1};
  int peer_fd_{-1};
};

universal_gnss_ntrip::NtripConfig MakeConfig()
{
  universal_gnss_ntrip::NtripConfig config;
  config.host = "caster.example.com";
  config.port = 2101u;
  config.mountpoint = "RTCM32";
  config.user_agent = "universal-gnss-test";
  config.reconnect_policy.initial_delay_ms = 1000u;
  config.reconnect_policy.max_delay_ms = 8000u;
  config.reconnect_policy.multiplier = 2.0;
  config.reconnect_policy.max_attempts = 4u;
  return config;
}

void TestNtripClientReconnectStateOnFailure(TestContext& ctx)
{
  SocketPair sockets;
  ctx.Expect(sockets.Open(), "socketpair fixture should open for the reconnect-state test");

  universal_gnss_ntrip::NtripClient client(MakeConfig());
  ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) ==
                 universal_gnss_ntrip::NtripClientError::kNone,
             "adopting a connected socket should succeed before injecting a bad NTRIP response");
  ctx.Expect(client.SendRequest() == universal_gnss_ntrip::NtripClientError::kNone,
             "sending the NTRIP request should succeed before the reconnect failure test");
  sockets.ReadPeerExact(client.request().request_text.size());

  ctx.Expect(sockets.WritePeer("NOT_A_HTTP_RESPONSE\r\n\r\n"),
             "the fake peer should send an invalid response header");

  std::vector<std::uint8_t> buffer(64u, 0u);
  const auto read_result = client.Read(buffer.data(), buffer.size(), 3000000000LL);
  const auto& reconnect_state = client.reconnect_state();

  ctx.Expect(read_result.client_error == universal_gnss_ntrip::NtripClientError::kProtocol &&
                 client.state() == universal_gnss_ntrip::NtripClientState::kFailed,
             "invalid NTRIP responses should fail the client");
  ctx.Expect(client.metrics().reconnect_count == 1u &&
                 reconnect_state.attempt_count == 1u &&
                 reconnect_state.current_delay_ms == 1000u,
             "retry-worthy client failures should increment reconnect metrics and record the first delay");
  ctx.Expect(reconnect_state.last_failure_time_ns == 3000000000LL &&
                 reconnect_state.next_attempt_time_ns == 4000000000LL,
             "client reconnect state should capture the failure time and next retry deadline");
}

#endif

}  // namespace

int main()
{
  TestContext ctx;

  TestFirstFailureSchedulesInitialDelay(ctx);
  TestExponentialBackoffAndMaxDelay(ctx);
  TestMaxAttemptsStopsScheduling(ctx);
  TestSuccessResetBehavior(ctx);
  TestSuccessKeepsStateWhenResetDisabled(ctx);
  TestDisabledPolicyNeverReconnects(ctx);

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  TestNtripClientReconnectStateOnFailure(ctx);
#endif

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_ntrip reconnect policy tests passed\n";
  return EXIT_SUCCESS;
}
