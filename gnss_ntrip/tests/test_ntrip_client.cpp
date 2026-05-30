#include <csignal>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)

#include <sys/socket.h>
#include <unistd.h>

#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_ntrip/ntrip_client.hpp"
#include "universal_gnss_ntrip/gga_generator.hpp"
#include "universal_gnss_ntrip/ntrip_request.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/nmea_parser.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"

namespace
{

using universal_gnss::GnssDiagnosticSeverity;
using universal_gnss::GnssFixType;
using universal_gnss_ntrip::NtripClient;
using universal_gnss_ntrip::NtripClientError;
using universal_gnss_ntrip::NtripGgaSendError;
using universal_gnss_ntrip::NtripGgaSendStatus;
using universal_gnss_ntrip::NtripClientState;
using universal_gnss_ntrip::NtripConfig;
using universal_gnss_ntrip::NtripVersion;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaGgaFixQuality;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::NmeaSentenceFramer;
using universal_gnss_protocols::ParserStatus;

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

  bool WritePeer(const std::vector<std::uint8_t>& data)
  {
    std::size_t offset = 0u;
    while (offset < data.size())
    {
      const ssize_t bytes_written =
          ::write(peer_fd_,
                  data.data() + static_cast<std::ptrdiff_t>(offset),
                  data.size() - offset);
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

  bool WritePeer(const std::string& text)
  {
    return WritePeer(std::vector<std::uint8_t>(text.begin(), text.end()));
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

std::vector<std::uint8_t> BuildRtcmFrame(const std::uint16_t message_type,
                                         const bool valid_crc = true)
{
  const std::vector<std::uint8_t> payload = {
      static_cast<std::uint8_t>((message_type >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((message_type & 0x0Fu) << 4u),
  };

  std::vector<std::uint8_t> bytes = {0xD3u, 0x00u,
                                     static_cast<std::uint8_t>(payload.size())};
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  if (!valid_crc)
  {
    crc ^= 0x1u;
  }

  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

void Append(std::vector<std::uint8_t>& destination, const std::vector<std::uint8_t>& source)
{
  destination.insert(destination.end(), source.begin(), source.end());
}

NtripConfig MakeConfig()
{
  NtripConfig config;
  config.host = "caster.example.com";
  config.port = 2101u;
  config.mountpoint = "RTCM32";
  config.user_agent = "universal-gnss-test";
  config.version = NtripVersion::kV2;
  return config;
}

universal_gnss::GnssRuntimeState MakeRuntimeState()
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = GnssFixType::kFix;
  state.latitude_deg = 48.1173;
  state.longitude_deg = 11.5166667;
  state.altitude_m = 545.4;
  state.hdop = 0.9f;
  state.satellites_used = 8u;
  return state;
}

NmeaSentence FrameNmeaSentence(const std::string& text)
{
  NmeaSentenceFramer framer;
  universal_gnss_protocols::ParserResult<NmeaSentence> result;
  for (const char ch : text)
  {
    result = framer.PushByte(static_cast<std::uint8_t>(ch));
  }

  if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
  {
    std::cerr << "FAILED: test setup could not frame generated GGA bytes\n";
    std::exit(EXIT_FAILURE);
  }

  return *result.record;
}

void TestRequestAndStreamingFlow(TestContext& ctx)
{
  SocketPair sockets;
  ctx.Expect(sockets.Open(), "socketpair fixture should open for request/streaming test");

  NtripClient client(MakeConfig());
  universal_gnss_transport::TcpClientConfig tcp_config;
  tcp_config.read_timeout_ms = 100u;
  client.set_tcp_config(tcp_config);

  ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone &&
                 client.state() == NtripClientState::kConnected &&
                 client.IsConnected() &&
                 client.metrics().connected,
             "adopting a connected socket should move the NTRIP client into the connected state");

  ctx.Expect(client.SendRequest() == NtripClientError::kNone &&
                 client.metrics().request_sent &&
                 client.metrics().bytes_sent == client.request().request_text.size(),
             "sending the NTRIP request should write bytes and mark the request as sent");

  const auto peer_request = sockets.ReadPeerExact(client.request().request_text.size());
  const std::string request_text(peer_request.begin(), peer_request.end());
  ctx.Expect(request_text == client.request().request_text,
             "the peer should receive the exact formatted NTRIP GET request");

  std::vector<std::uint8_t> payload;
  Append(payload, BuildRtcmFrame(1005u));
  Append(payload, BuildRtcmFrame(1077u));

  std::vector<std::uint8_t> response;
  const std::string header = "ICY 200 OK\r\nNtrip-Version: Ntrip/2.0\r\n\r\n";
  response.insert(response.end(), header.begin(), header.end());
  Append(response, payload);
  ctx.Expect(sockets.WritePeer(response), "the fake peer should send a valid NTRIP response and RTCM payload");

  std::vector<std::uint8_t> read_buffer(128u, 0u);
  const auto read_result = client.Read(read_buffer.data(), read_buffer.size(), 1000000000LL);
  read_buffer.resize(read_result.bytes_read);

  ctx.Expect(read_result.client_error == NtripClientError::kNone &&
                 read_result.transport_status == universal_gnss_transport::TransportStatus::kOk &&
                 read_result.bytes_read == payload.size() &&
                 read_buffer == payload,
             "reading after a valid ICY response should return only the RTCM payload bytes");
  ctx.Expect(client.state() == NtripClientState::kStreaming &&
                 client.metrics().response_received &&
                 client.response_header() == header,
             "a valid NTRIP response should move the client into streaming state and capture the header");
  ctx.Expect(client.metrics().bytes_received == response.size() &&
                 client.metrics().rtcm_frames_seen == 2u &&
                 client.metrics().rtcm_frames_received == 2u &&
                 client.metrics().invalid_rtcm_frames == 0u &&
                 client.metrics().last_rtcm_message_type == 1077u,
             "streaming should update byte counters and RTCM frame metrics");
  ctx.Expect(client.correction_monitor().MessageCount(1005u) == 1u &&
                 client.correction_monitor().MessageCount(1077u) == 1u &&
                 client.correction_monitor().HasSeenBasePosition1005() &&
                 client.correction_monitor().HasSeenAnyMsmMessage(),
             "the RTCM correction monitor should be fed from streamed payload bytes");

  universal_gnss_protocols::RtcmCorrectionHealthOptions health_options;
  health_options.now_timestamp_ns = 1000000000LL;
  health_options.stale_after_ns = 5000000000LL;
  health_options.required_message_types = {1005u, 1077u};
  health_options.require_base_position = true;
  health_options.require_any_msm = true;
  const auto health = client.BuildCorrectionHealth(health_options);

  ctx.Expect(health.overall_severity == GnssDiagnosticSeverity::kOk &&
                 health.correction_available &&
                 !health.stale_data,
             "correction health should report an active RTCM stream when required messages are present");
}

void TestSplitHttpResponseAndDisconnect(TestContext& ctx)
{
  SocketPair sockets;
  ctx.Expect(sockets.Open(), "socketpair fixture should open for split-response test");

  NtripClient client(MakeConfig());
  universal_gnss_transport::TcpClientConfig tcp_config;
  tcp_config.read_timeout_ms = 50u;
  client.set_tcp_config(tcp_config);

  ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
             "adopting a connected socket should succeed for the split-response test");
  ctx.Expect(client.SendRequest() == NtripClientError::kNone,
             "sending the request should succeed before a split HTTP response");
  sockets.ReadPeerExact(client.request().request_text.size());

  ctx.Expect(sockets.WritePeer("HTTP/1.1 200 OK\r\nServer: fake\r\n"),
             "the fake peer should send the first half of the HTTP response");

  std::vector<std::uint8_t> read_buffer(64u, 0u);
  const auto first_read = client.Read(read_buffer.data(), read_buffer.size(), 2000000000LL);
  ctx.Expect(first_read.client_error == NtripClientError::kNone &&
                 first_read.bytes_read == 0u &&
                 client.state() == NtripClientState::kConnected &&
                 !client.metrics().response_received,
             "a partial HTTP response should keep the client connected until the header terminator arrives");

  const auto payload = BuildRtcmFrame(1087u);
  std::vector<std::uint8_t> second_chunk = {'\r', '\n'};
  Append(second_chunk, payload);
  ctx.Expect(sockets.WritePeer(second_chunk),
             "the fake peer should send the remaining header terminator and payload");

  const auto second_read = client.Read(read_buffer.data(), read_buffer.size(), 2000000000LL);
  read_buffer.resize(second_read.bytes_read);
  ctx.Expect(second_read.client_error == NtripClientError::kNone &&
                 second_read.bytes_read == payload.size() &&
                 read_buffer == payload &&
                 client.state() == NtripClientState::kStreaming &&
                 client.metrics().response_received &&
                 client.metrics().last_rtcm_message_type == 1087u,
             "the client should transition into streaming once the rest of the response header arrives");

  client.Disconnect();
  ctx.Expect(client.state() == NtripClientState::kDisconnected &&
                 !client.IsConnected() &&
                 !client.metrics().connected &&
                 client.metrics().last_error == NtripClientError::kNone,
             "explicit disconnect should close the client cleanly without forcing an error state");
}

void TestInvalidResponsesAndConnectFailure(TestContext& ctx)
{
  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for non-200 response test");

    NtripClient client(MakeConfig());
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone &&
                   client.SendRequest() == NtripClientError::kNone,
               "setup should succeed before testing an HTTP error response");
    sockets.ReadPeerExact(client.request().request_text.size());

    ctx.Expect(sockets.WritePeer("HTTP/1.1 404 Not Found\r\n\r\n"),
               "the fake peer should send a non-200 response");
    std::vector<std::uint8_t> buffer(32u, 0u);
    const auto read_result = client.Read(buffer.data(), buffer.size(), 3000000000LL);

    ctx.Expect(read_result.client_error == NtripClientError::kHttp &&
                   client.state() == NtripClientState::kFailed &&
                   !client.metrics().connected &&
                   !client.metrics().response_received &&
                   client.metrics().last_error == NtripClientError::kHttp,
               "non-200 NTRIP responses should fail with an HTTP error");
  }

  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for invalid-response test");

    NtripClient client(MakeConfig());
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone &&
                   client.SendRequest() == NtripClientError::kNone,
               "setup should succeed before testing an invalid response");
    sockets.ReadPeerExact(client.request().request_text.size());

    ctx.Expect(sockets.WritePeer("NOT_A_HTTP_RESPONSE\r\n\r\n"),
               "the fake peer should send an invalid response header");
    std::vector<std::uint8_t> buffer(32u, 0u);
    const auto read_result = client.Read(buffer.data(), buffer.size(), 4000000000LL);

    ctx.Expect(read_result.client_error == NtripClientError::kProtocol &&
                   client.state() == NtripClientState::kFailed &&
                   client.metrics().last_error == NtripClientError::kProtocol,
               "invalid response headers should fail with a protocol error");
  }

  {
    NtripConfig invalid = MakeConfig();
    invalid.host.clear();
    NtripClient client(invalid);
    ctx.Expect(client.Connect() == NtripClientError::kConfiguration &&
                   client.state() == NtripClientState::kFailed &&
                   client.metrics().last_error == NtripClientError::kConfiguration,
               "missing host configuration should fail before opening a TCP connection");
  }

  {
    NtripConfig invalid = MakeConfig();
    invalid.host = "256.256.256.256";
    NtripClient client(invalid);
    ctx.Expect(client.Connect() == NtripClientError::kDisconnected &&
                   client.state() == NtripClientState::kFailed &&
                   !client.metrics().connected,
               "unresolvable TCP hosts should fail the connect step cleanly");
  }
}

void TestExplicitAndPolicyDrivenGgaSending(TestContext& ctx)
{
  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for explicit GGA send test");

    NtripClient client(MakeConfig());
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
               "adopting a connected socket should succeed before sending GGA");

    const auto expected_sentence =
        universal_gnss_ntrip::GenerateGgaFromRuntimeState(MakeRuntimeState()).sentence;
    const auto send_result = client.SendGga(MakeRuntimeState(), 123456789LL);
    ctx.Expect(send_result.status == NtripGgaSendStatus::kSent && send_result.sent(),
               "SendGga should synchronously write a generated GGA sentence");

    const auto peer_bytes = sockets.ReadPeerExact(expected_sentence.size());
    const std::string peer_text(peer_bytes.begin(), peer_bytes.end());
    ctx.Expect(peer_text == expected_sentence,
               "SendGga should write the exact generated GGA sentence to the transport");

    const auto framed = FrameNmeaSentence(peer_text);
    const auto parsed = universal_gnss_protocols::ParseNmeaGga(framed);
    ctx.Expect(framed.checksum_status == ChecksumStatus::kValid &&
                   parsed.status == ParserStatus::kRecordReady &&
                   parsed.record.has_value() &&
                   parsed.record->fix_quality == NmeaGgaFixQuality::kGpsFix,
               "SendGga should emit a valid checksum-protected GGA sentence");
    ctx.Expect(client.metrics().gga_sent_count == 1u &&
                   client.metrics().gga_send_errors == 0u &&
                   client.metrics().last_gga_sent_timestamp_ns ==
                       std::optional<std::int64_t>(123456789LL) &&
                   !client.metrics().last_gga_error.has_value() &&
                   client.gga_injection_policy().last_sent_timestamp_ns ==
                       std::optional<std::int64_t>(123456789LL),
               "successful GGA sends should update metrics and the injection policy timestamp");
  }

  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for disabled-policy GGA test");

    NtripConfig config = MakeConfig();
    config.send_gga = false;
    NtripClient client(config);
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
               "adopting a connected socket should succeed before disabled-policy GGA test");

    const auto maybe_result = client.MaybeSendGga(MakeRuntimeState(), 1000000000LL);
    ctx.Expect(maybe_result.status == NtripGgaSendStatus::kSkippedDisabled &&
                   maybe_result.skipped() &&
                   client.metrics().gga_sent_count == 0u &&
                   client.metrics().bytes_sent == 0u,
               "MaybeSendGga should no-op cleanly when the policy is disabled");
  }

  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for GGA interval test");

    NtripConfig config = MakeConfig();
    config.send_gga = true;
    config.gga_interval_s = 5u;
    NtripClient client(config);
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
               "adopting a connected socket should succeed before interval-based GGA sends");

    const auto expected_sentence =
        universal_gnss_ntrip::GenerateGgaFromRuntimeState(MakeRuntimeState()).sentence;
    const auto first_send = client.MaybeSendGga(MakeRuntimeState(), 1000000000LL);
    ctx.Expect(first_send.status == NtripGgaSendStatus::kSent,
               "MaybeSendGga should send the first eligible GGA sentence");
    sockets.ReadPeerExact(expected_sentence.size());

    const auto second_send = client.MaybeSendGga(MakeRuntimeState(), 4000000000LL);
    ctx.Expect(second_send.status == NtripGgaSendStatus::kSkippedInterval &&
                   client.metrics().gga_sent_count == 1u &&
                   client.metrics().last_gga_sent_timestamp_ns ==
                       std::optional<std::int64_t>(1000000000LL),
               "MaybeSendGga should suppress GGA writes until the interval elapses");
  }

  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for fix-required GGA test");

    NtripConfig config = MakeConfig();
    config.send_gga = true;
    NtripClient client(config);
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
               "adopting a connected socket should succeed before position-required GGA test");

    auto no_fix_state = MakeRuntimeState();
    no_fix_state.fix_valid = false;
    no_fix_state.fix_type = GnssFixType::kNoFix;

    const auto maybe_result = client.MaybeSendGga(no_fix_state, 1000000000LL);
    ctx.Expect(maybe_result.status == NtripGgaSendStatus::kSkippedPositionRequired &&
                   client.metrics().gga_sent_count == 0u &&
                   client.metrics().gga_send_errors == 0u,
               "MaybeSendGga should no-op when the policy requires a position fix and none exists");
  }

  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for invalid-state GGA test");

    NtripConfig config = MakeConfig();
    config.send_gga = true;
    NtripClient client(config);
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
               "adopting a connected socket should succeed before invalid-state GGA test");

    auto invalid_state = MakeRuntimeState();
    invalid_state.longitude_deg.reset();

    const auto send_result = client.MaybeSendGga(invalid_state, 1000000000LL);
    ctx.Expect(send_result.status == NtripGgaSendStatus::kError &&
                   send_result.send_error ==
                       std::optional<NtripGgaSendError>(NtripGgaSendError::kGenerationFailed) &&
                   send_result.generation_error ==
                       std::optional<universal_gnss_ntrip::GgaGenerationError>(
                           universal_gnss_ntrip::GgaGenerationError::kMissingLongitude) &&
                   client.metrics().gga_send_errors == 1u &&
                   client.metrics().last_gga_error ==
                       std::optional<NtripGgaSendError>(NtripGgaSendError::kGenerationFailed) &&
                   client.state() == NtripClientState::kConnected,
               "invalid runtime state should increment GGA error metrics without crashing the client");
  }

  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for GGA write-failure test");

    NtripClient client(MakeConfig());
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
               "adopting a connected socket should succeed before write-failure GGA test");

    const auto previous_handler = std::signal(SIGPIPE, SIG_IGN);
    sockets.ClosePeer();
    const auto send_result = client.SendGga(MakeRuntimeState(), 2000000000LL);
    std::signal(SIGPIPE, previous_handler);

    ctx.Expect(send_result.status == NtripGgaSendStatus::kError &&
                   send_result.client_error == NtripClientError::kDisconnected &&
                   send_result.send_error ==
                       std::optional<NtripGgaSendError>(NtripGgaSendError::kWriteFailure) &&
                   client.metrics().gga_send_errors == 1u &&
                   client.metrics().last_gga_error ==
                       std::optional<NtripGgaSendError>(NtripGgaSendError::kWriteFailure) &&
                   client.state() == NtripClientState::kFailed &&
                   client.metrics().last_error == NtripClientError::kDisconnected,
               "transport write failures should update GGA metrics and move the client into a failed state");
  }
}

}  // namespace

int main()
{
  TestContext ctx;

  TestRequestAndStreamingFlow(ctx);
  TestSplitHttpResponseAndDisconnect(ctx);
  TestInvalidResponsesAndConnectFailure(ctx);
  TestExplicitAndPolicyDrivenGgaSending(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_ntrip client tests passed\n";
  return EXIT_SUCCESS;
}

#else

int main()
{
  std::cout << "NTRIP client tests skipped on platforms without TCP client transport\n";
  return EXIT_SUCCESS;
}

#endif
