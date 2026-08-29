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
#include "universal_gnss_protocols/rtcm_crc24q.hpp"

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
                 state.current_delay_ms == 1000u &&
                 !state.exhausted,
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
  ctx.Expect(policy.NextDelay(state) == 100u,
             "NextDelay should return the initial delay before any reconnect failures");
  policy.OnFailure(state, 0LL);
  ctx.Expect(state.current_delay_ms == 100u,
             "the first reconnect delay should use the configured initial delay");

  ctx.Expect(policy.NextDelay(state) == 250u,
             "NextDelay should apply the configured multiplier and delay cap");
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
                 state.exhausted &&
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
                 !state.exhausted &&
                 !state.next_attempt_time_ns.has_value(),
             "disabled reconnect policies should leave the reconnect schedule empty");
}

void TestJitterFlagIsDeterministicForNow(TestContext& ctx)
{
  universal_gnss_ntrip::NtripReconnectPolicy policy;
  policy.initial_delay_ms = 200u;
  policy.max_delay_ms = 1000u;
  policy.multiplier = 2.0;
  policy.jitter_enabled = true;

  universal_gnss_ntrip::NtripReconnectState state;
  const auto first = policy.OnFailure(state, 1000000000LL);
  const auto second_delay = policy.NextDelay(state);

  ctx.Expect(first.current_delay_ms == 200u && second_delay == 400u,
             "jitter_enabled should remain deterministic and leave delay math unchanged for now");
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

  bool WritePeer(const std::vector<std::uint8_t>& data)
  {
    return WritePeer(std::string(data.begin(), data.end()));
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

std::vector<std::uint8_t> BuildRtcmFrame(const std::uint16_t message_type)
{
  const std::vector<std::uint8_t> payload = {
      static_cast<std::uint8_t>((message_type >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((message_type & 0x0Fu) << 4u),
  };
  std::vector<std::uint8_t> bytes = {0xD3u, 0x00u,
                                     static_cast<std::uint8_t>(payload.size())};
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  const std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

void ConfigureNonblockingReads(universal_gnss_ntrip::NtripClient& client)
{
  universal_gnss_transport::TcpClientConfig tcp_config;
  tcp_config.nonblocking = true;
  client.set_tcp_config(tcp_config);
}

bool AdoptAndSendRequest(TestContext& ctx,
                         SocketPair& sockets,
                         universal_gnss_ntrip::NtripClient& client,
                         const std::int64_t timestamp_ns)
{
  if (!sockets.Open())
  {
    ctx.Expect(false, "socketpair fixture should open for reconnect attempt");
    return false;
  }
  if (client.AdoptConnectedSocket(sockets.ReleaseClientFd()) !=
          universal_gnss_ntrip::NtripClientError::kNone ||
      client.SendRequest(timestamp_ns) != universal_gnss_ntrip::NtripClientError::kNone)
  {
    ctx.Expect(false, "TCP success should allow the NTRIP request to be sent");
    return false;
  }
  sockets.ReadPeerExact(client.request().request_text.size());
  return true;
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
                 reconnect_state.current_delay_ms == 1000u &&
                 !reconnect_state.exhausted,
             "retry-worthy client failures should increment reconnect metrics and record the first delay");
  ctx.Expect(reconnect_state.last_failure_time_ns == 3000000000LL &&
                 reconnect_state.next_attempt_time_ns == 4000000000LL,
             "client reconnect state should capture the failure time and next retry deadline");
}

void TestClientBackoffResetsOnlyAfterOperationalCorrectionFlow(TestContext& ctx)
{
  using universal_gnss_ntrip::NtripClientError;
  using universal_gnss_ntrip::NtripClientState;

  universal_gnss_ntrip::NtripConfig config = MakeConfig();
  config.reconnect_policy.max_attempts.reset();
  universal_gnss_ntrip::NtripClient client(config);
  ConfigureNonblockingReads(client);
  std::vector<std::uint8_t> buffer(256u, 0u);

  for (std::uint32_t attempt = 1u; attempt <= 3u; ++attempt)
  {
    SocketPair sockets;
    if (!AdoptAndSendRequest(ctx, sockets, client, attempt * 1000000000LL))
    {
      return;
    }
    ctx.Expect(sockets.WritePeer("HTTP/1.1 401 Unauthorized\r\n\r\n"),
               "fake caster should reject the application-level attempt");
    const auto result =
        client.Read(buffer.data(), buffer.size(), attempt * 1000000000LL + 1000000LL);
    const std::uint32_t expected_delay = 1000u << (attempt - 1u);
    ctx.Expect(result.client_error == NtripClientError::kHttp &&
                   client.state() == NtripClientState::kFailed &&
                   client.reconnect_state().attempt_count == attempt &&
                   client.reconnect_state().current_delay_ms == expected_delay,
               "TCP success followed by HTTP failure must preserve exponential backoff history");
  }

  {
    SocketPair sockets;
    if (!AdoptAndSendRequest(ctx, sockets, client, 5000000000LL))
    {
      return;
    }
    ctx.Expect(sockets.WritePeer("ICY 200 OK\r\n\r\n"),
               "fake caster should accept the NTRIP response without corrections");
    client.Read(buffer.data(), buffer.size(), 5000000000LL);
    sockets.ClosePeer();
    const auto disconnected = client.Read(buffer.data(), buffer.size(), 6000000000LL);
    ctx.Expect(disconnected.client_error == NtripClientError::kDisconnected &&
                   client.reconnect_state().attempt_count == 4u &&
                   client.reconnect_state().current_delay_ms == 8000u,
               "accepted response without correction flow must not reset reconnect history");
  }

  universal_gnss_ntrip::NtripClient short_stream_client(config);
  ConfigureNonblockingReads(short_stream_client);
  {
    SocketPair first_failure;
    if (!AdoptAndSendRequest(ctx, first_failure, short_stream_client, 1000000000LL))
    {
      return;
    }
    ctx.Expect(first_failure.WritePeer("HTTP/1.1 503 Unavailable\r\n\r\n"),
               "fake caster should seed reconnect history");
    short_stream_client.Read(buffer.data(), buffer.size(), 1001000000LL);
  }
  {
    SocketPair one_frame_stream;
    if (!AdoptAndSendRequest(ctx, one_frame_stream, short_stream_client, 3000000000LL))
    {
      return;
    }
    std::vector<std::uint8_t> response{'I', 'C', 'Y', ' ', '2', '0', '0', ' ', 'O', 'K',
                                       '\r', '\n', '\r', '\n'};
    const auto frame = BuildRtcmFrame(1077u);
    response.insert(response.end(), frame.begin(), frame.end());
    ctx.Expect(one_frame_stream.WritePeer(response),
               "fake caster should send one valid RTCM frame then drop");
    short_stream_client.Read(buffer.data(), buffer.size(), 3000000000LL);
    one_frame_stream.ClosePeer();
    short_stream_client.Read(buffer.data(), buffer.size(), 4000000000LL);
    ctx.Expect(short_stream_client.reconnect_state().attempt_count == 2u &&
                   short_stream_client.reconnect_state().current_delay_ms == 2000u,
               "one-frame-then-drop stream must not be operational enough to reset backoff");
  }

  universal_gnss_ntrip::NtripClient operational_client(config);
  ConfigureNonblockingReads(operational_client);
  {
    SocketPair seed_failure;
    if (!AdoptAndSendRequest(ctx, seed_failure, operational_client, 1000000000LL))
    {
      return;
    }
    ctx.Expect(seed_failure.WritePeer("HTTP/1.1 503 Unavailable\r\n\r\n"),
               "fake caster should seed operational-stream backoff history");
    operational_client.Read(buffer.data(), buffer.size(), 1001000000LL);
  }
  {
    SocketPair operational_stream;
    if (!AdoptAndSendRequest(ctx, operational_stream, operational_client, 3000000000LL))
    {
      return;
    }
    std::vector<std::uint8_t> response{'I', 'C', 'Y', ' ', '2', '0', '0', ' ', 'O', 'K',
                                       '\r', '\n', '\r', '\n'};
    const auto frame = BuildRtcmFrame(1077u);
    response.insert(response.end(), frame.begin(), frame.end());
    response.insert(response.end(), frame.begin(), frame.end());
    ctx.Expect(operational_stream.WritePeer(response),
               "fake caster should demonstrate sustained correction flow");
    operational_client.Read(buffer.data(), buffer.size(), 3000000000LL);
    ctx.Expect(operational_client.reconnect_state().attempt_count == 0u,
               "two valid RTCM frames should declare the NTRIP attempt operational");
    operational_stream.ClosePeer();
    operational_client.Read(buffer.data(), buffer.size(), 4000000000LL);
    ctx.Expect(operational_client.reconnect_state().attempt_count == 1u &&
                   operational_client.reconnect_state().current_delay_ms == 1000u,
               "failure after operational flow should restart from normal backoff");
  }
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
  TestJitterFlagIsDeterministicForNow(ctx);

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  TestNtripClientReconnectStateOnFailure(ctx);
  TestClientBackoffResetsOnlyAfterOperationalCorrectionFlow(ctx);
#endif

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_ntrip reconnect policy tests passed\n";
  return EXIT_SUCCESS;
}
