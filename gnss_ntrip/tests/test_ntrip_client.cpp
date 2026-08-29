#include <csignal>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
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
#include "universal_gnss_protocols/rtcm_parser.hpp"

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

void AppendBit(std::vector<std::uint8_t>& payload, std::size_t& bit_offset, const bool bit)
{
  if ((bit_offset % 8u) == 0u)
  {
    payload.push_back(0u);
  }

  if (bit)
  {
    payload.back() |= static_cast<std::uint8_t>(1u << (7u - (bit_offset % 8u)));
  }
  ++bit_offset;
}

void AppendUnsignedBits(std::vector<std::uint8_t>& payload,
                        std::size_t& bit_offset,
                        const std::uint64_t value,
                        const std::size_t bit_count)
{
  for (std::size_t i = 0u; i < bit_count; ++i)
  {
    const std::size_t shift = bit_count - 1u - i;
    AppendBit(payload, bit_offset, ((value >> shift) & 0x01u) != 0u);
  }
}

void AppendZeroBits(std::vector<std::uint8_t>& payload,
                    std::size_t& bit_offset,
                    const std::size_t bit_count)
{
  for (std::size_t index = 0u; index < bit_count; ++index)
  {
    AppendBit(payload, bit_offset, false);
  }
}

std::size_t GetRtcmMsmBodyBits(const std::uint8_t msm_variant,
                               const std::size_t satellite_count,
                               const std::size_t populated_cell_count)
{
  switch (msm_variant)
  {
    case 4u:
      return satellite_count * 18u + populated_cell_count * 48u;
    case 5u:
      return satellite_count * 36u + populated_cell_count * 63u;
    case 6u:
      return satellite_count * 18u + populated_cell_count * 65u;
    case 7u:
      return satellite_count * 36u + populated_cell_count * 80u;
    default:
      return 0u;
  }
}

std::vector<std::uint8_t> BuildRtcmFrameFromPayload(const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> bytes = {
      0xD3u,
      static_cast<std::uint8_t>((payload.size() >> 8u) & 0x03u),
      static_cast<std::uint8_t>(payload.size() & 0xFFu),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

std::vector<std::uint8_t> BuildRtcmMsmFrame(const std::uint16_t message_type,
                                            const std::uint16_t station_id,
                                            const std::vector<std::uint8_t>& satellite_ids,
                                            const std::vector<std::uint8_t>& signal_ids,
                                            const std::vector<bool>& cell_mask)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;

  AppendUnsignedBits(payload, bit_offset, message_type, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, 123456u, 30u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 3u);
  AppendUnsignedBits(payload, bit_offset, 15u, 7u);
  AppendUnsignedBits(payload, bit_offset, 1u, 2u);
  AppendUnsignedBits(payload, bit_offset, 0u, 2u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 3u, 3u);

  for (std::uint8_t satellite = 1u; satellite <= 64u; ++satellite)
  {
    bool present = false;
    for (const auto candidate : satellite_ids)
    {
      if (candidate == satellite)
      {
        present = true;
        break;
      }
    }
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
  }

  for (std::uint8_t signal = 1u; signal <= 32u; ++signal)
  {
    bool present = false;
    for (const auto candidate : signal_ids)
    {
      if (candidate == signal)
      {
        present = true;
        break;
      }
    }
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
  }

  std::size_t populated_cell_count = 0u;
  for (const bool present : cell_mask)
  {
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
    if (present)
    {
      ++populated_cell_count;
    }
  }

  AppendZeroBits(payload,
                 bit_offset,
                 GetRtcmMsmBodyBits(universal_gnss_protocols::GetRtcmMsmVariant(message_type),
                                    satellite_ids.size(),
                                    populated_cell_count));
  return BuildRtcmFrameFromPayload(payload);
}

std::vector<std::uint8_t> BuildRtcm1005Frame(const std::uint16_t station_id)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;
  AppendUnsignedBits(payload, bit_offset, 1005u, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendZeroBits(payload, bit_offset, 128u);
  return BuildRtcmFrameFromPayload(payload);
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

void ConfigureNonblockingReads(NtripClient& client)
{
  universal_gnss_transport::TcpClientConfig tcp_config;
  tcp_config.nonblocking = true;
  client.set_tcp_config(tcp_config);
}

bool BeginStreaming(TestContext& ctx,
                    SocketPair& sockets,
                    NtripClient& client,
                    const std::int64_t timestamp_ns,
                    const std::vector<std::uint8_t>& initial_payload = {})
{
  if (!sockets.Open())
  {
    ctx.Expect(false, "socketpair fixture should open before starting an NTRIP stream");
    return false;
  }

  if (client.AdoptConnectedSocket(sockets.ReleaseClientFd()) != NtripClientError::kNone ||
      client.SendRequest(timestamp_ns) != NtripClientError::kNone)
  {
    ctx.Expect(false, "adopted NTRIP setup should connect and send its request");
    return false;
  }
  sockets.ReadPeerExact(client.request().request_text.size());

  std::vector<std::uint8_t> response{
      'I', 'C', 'Y', ' ', '2', '0', '0', ' ', 'O', 'K', '\r', '\n', '\r', '\n'};
  response.insert(response.end(), initial_payload.begin(), initial_payload.end());
  if (!sockets.WritePeer(response))
  {
    ctx.Expect(false, "fake caster should write its accepted response");
    return false;
  }

  std::vector<std::uint8_t> buffer(4096u, 0u);
  const auto read_result = client.Read(buffer.data(), buffer.size(), timestamp_ns);
  const bool streaming = read_result.client_error == NtripClientError::kNone &&
                         client.state() == NtripClientState::kStreaming;
  ctx.Expect(streaming, "a valid NTRIP response should enter Streaming for liveness tests");
  return streaming;
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
  Append(payload, BuildRtcmMsmFrame(1077u, 42u, {1u}, {2u}, {true}));

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

void TestLegacyIcyResponseWithoutBlankLine(TestContext& ctx)
{
  SocketPair sockets;
  ctx.Expect(sockets.Open(), "socketpair fixture should open for legacy ICY response test");

  NtripClient client(MakeConfig());
  universal_gnss_transport::TcpClientConfig tcp_config;
  tcp_config.read_timeout_ms = 100u;
  client.set_tcp_config(tcp_config);

  ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
             "adopting a connected socket should succeed for the legacy ICY response test");
  ctx.Expect(client.SendRequest() == NtripClientError::kNone,
             "sending the request should succeed before a legacy ICY response");
  sockets.ReadPeerExact(client.request().request_text.size());

  const auto payload = BuildRtcmFrame(1005u);
  std::vector<std::uint8_t> response;
  const std::string header = "ICY 200 OK\r\n";
  response.insert(response.end(), header.begin(), header.end());
  Append(response, payload);
  ctx.Expect(sockets.WritePeer(response),
             "the fake peer should send a legacy ICY response line followed directly by RTCM payload");

  std::vector<std::uint8_t> read_buffer(128u, 0u);
  const auto read_result = client.Read(read_buffer.data(), read_buffer.size(), 2500000000LL);
  read_buffer.resize(read_result.bytes_read);

  ctx.Expect(read_result.client_error == NtripClientError::kNone &&
                 read_result.transport_status == universal_gnss_transport::TransportStatus::kOk &&
                 read_result.bytes_read == payload.size() &&
                 read_buffer == payload,
             "legacy ICY responses without a blank header terminator should still return the RTCM payload");
  ctx.Expect(client.state() == NtripClientState::kStreaming &&
                 client.metrics().response_received &&
                 client.response_header() == header,
             "legacy ICY responses should still transition the client into streaming state");
  ctx.Expect(client.metrics().rtcm_frames_seen == 1u &&
                 client.metrics().rtcm_frames_received == 1u &&
                 client.metrics().invalid_rtcm_frames == 0u &&
                 client.metrics().last_rtcm_message_type == 1005u,
             "legacy ICY responses should still feed RTCM frame metrics");
}

void TestSplitLegacyIcyResponseWithoutBlankLine(TestContext& ctx)
{
  SocketPair sockets;
  ctx.Expect(sockets.Open(), "socketpair fixture should open for split legacy ICY response test");

  NtripClient client(MakeConfig());
  universal_gnss_transport::TcpClientConfig tcp_config;
  tcp_config.read_timeout_ms = 100u;
  client.set_tcp_config(tcp_config);

  ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
             "adopting a connected socket should succeed for the split legacy ICY response test");
  ctx.Expect(client.SendRequest() == NtripClientError::kNone,
             "sending the request should succeed before a split legacy ICY response");
  sockets.ReadPeerExact(client.request().request_text.size());

  ctx.Expect(sockets.WritePeer("ICY 200 OK\r\n"),
             "the fake peer should send the legacy ICY status line first");

  std::vector<std::uint8_t> read_buffer(128u, 0u);
  const auto first_read = client.Read(read_buffer.data(), read_buffer.size(), 2600000000LL);
  ctx.Expect(first_read.client_error == NtripClientError::kNone &&
                 first_read.bytes_read == 0u &&
                 client.state() == NtripClientState::kConnected &&
                 !client.metrics().response_received,
             "a standalone legacy ICY status line should keep the client connected until payload arrives");

  const auto payload = BuildRtcmFrame(1077u);
  ctx.Expect(sockets.WritePeer(payload),
             "the fake peer should send RTCM payload bytes after the standalone legacy ICY line");

  const auto second_read = client.Read(read_buffer.data(), read_buffer.size(), 2700000000LL);
  read_buffer.resize(second_read.bytes_read);
  ctx.Expect(second_read.client_error == NtripClientError::kNone &&
                 second_read.transport_status == universal_gnss_transport::TransportStatus::kOk &&
                 second_read.bytes_read == payload.size() &&
                 read_buffer == payload &&
                 client.state() == NtripClientState::kStreaming &&
                 client.metrics().response_received &&
                 client.response_header() == "ICY 200 OK\r\n",
             "a split legacy ICY response should still transition into streaming when the payload arrives");
}

void TestLegacyIcyResponseWithMidFrameBinary(TestContext& ctx)
{
  SocketPair sockets;
  ctx.Expect(sockets.Open(), "socketpair fixture should open for mid-frame legacy ICY response test");

  NtripClient client(MakeConfig());
  universal_gnss_transport::TcpClientConfig tcp_config;
  tcp_config.read_timeout_ms = 100u;
  client.set_tcp_config(tcp_config);

  ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
             "adopting a connected socket should succeed for the mid-frame legacy ICY response test");
  ctx.Expect(client.SendRequest() == NtripClientError::kNone,
             "sending the request should succeed before a mid-frame legacy ICY response");
  sockets.ReadPeerExact(client.request().request_text.size());

  const std::vector<std::uint8_t> payload = {0x50u, 0x81u, 0x42u, 0x00u};
  std::vector<std::uint8_t> response;
  const std::string header = "ICY 200 OK\r\n";
  response.insert(response.end(), header.begin(), header.end());
  Append(response, payload);
  ctx.Expect(sockets.WritePeer(response),
             "the fake peer should send a legacy ICY response line followed by binary payload that does not start on an RTCM frame boundary");

  std::vector<std::uint8_t> read_buffer(128u, 0u);
  const auto read_result = client.Read(read_buffer.data(), read_buffer.size(), 2800000000LL);
  read_buffer.resize(read_result.bytes_read);

  ctx.Expect(read_result.client_error == NtripClientError::kNone &&
                 read_result.transport_status == universal_gnss_transport::TransportStatus::kOk &&
                 read_result.bytes_read == payload.size() &&
                 read_buffer == payload,
             "legacy ICY responses should still enter streaming when binary payload bytes arrive mid-frame");
  ctx.Expect(client.state() == NtripClientState::kStreaming &&
                 client.metrics().response_received &&
                 client.response_header() == header,
             "binary payload after a legacy ICY line should still mark the response as received");
}

void TestNtripStatusCodeTokenValidation(TestContext& ctx)
{
  const std::vector<std::string> accepted_headers = {
      "HTTP/1.0 200 OK\r\n\r\n",
      "HTTP/1.1 200\r\n\r\n",
      "ICY 200 OK\r\n\r\n",
  };

  for (const std::string& header : accepted_headers)
  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for accepted-status test");

    NtripClient client(MakeConfig());
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone &&
                   client.SendRequest() == NtripClientError::kNone,
               "setup should succeed before testing an accepted NTRIP status line");
    sockets.ReadPeerExact(client.request().request_text.size());

    const auto payload = BuildRtcmFrame(1005u);
    std::vector<std::uint8_t> response(header.begin(), header.end());
    Append(response, payload);
    ctx.Expect(sockets.WritePeer(response),
               "the fake peer should send an accepted status line with same-read RTCM payload");

    std::vector<std::uint8_t> buffer(64u, 0u);
    const auto read_result = client.Read(buffer.data(), buffer.size(), 5000000000LL);
    buffer.resize(read_result.bytes_read);
    ctx.Expect(read_result.client_error == NtripClientError::kNone &&
                   client.state() == NtripClientState::kStreaming &&
                   client.metrics().response_received && buffer == payload &&
                   client.metrics().rtcm_frames_received == 1u,
               "an exact successful NTRIP status token should enter streaming and preserve same-read RTCM payload");
  }

  const std::vector<std::string> rejected_headers = {
      "HTTP/1.1 2000 Not OK\r\n\r\n",
      "HTTP/1.1 200X Not OK\r\n\r\n",
      "HTTP/1.1 20\r\n\r\n",
      "HTTP/1.1 401 Unauthorized\r\n\r\n",
      "ICY 200anything\r\n\r\n",
      "ICY 200anything\r\n",
  };

  for (const std::string& header : rejected_headers)
  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for rejected-status test");

    NtripClient client(MakeConfig());
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone &&
                   client.SendRequest() == NtripClientError::kNone,
               "setup should succeed before testing a rejected NTRIP status line");
    sockets.ReadPeerExact(client.request().request_text.size());

    const auto payload = BuildRtcmFrame(1005u);
    std::vector<std::uint8_t> response(header.begin(), header.end());
    Append(response, payload);
    ctx.Expect(sockets.WritePeer(response),
               "the fake peer should send a rejected status line with RTCM-looking suffix bytes");

    std::vector<std::uint8_t> buffer(64u, 0u);
    const auto read_result = client.Read(buffer.data(), buffer.size(), 6000000000LL);
    ctx.Expect(read_result.client_error == NtripClientError::kHttp &&
                   read_result.bytes_read == 0u && client.state() == NtripClientState::kFailed &&
                   !client.metrics().response_received &&
                   client.metrics().rtcm_frames_seen == 0u &&
                   client.correction_monitor().MessageCount(1005u) == 0u,
               "malformed/non-200 NTRIP status tokens must fail without consuming RTCM suffix bytes");
  }
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
    std::vector<std::uint8_t> buffer(64u, 0u);
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

void TestCorrectionFlowLivenessUsesCompleteValidFrames(TestContext& ctx)
{
  constexpr std::int64_t kSecond = 1000000000LL;

  {
    SocketPair sockets;
    NtripConfig config = MakeConfig();
    config.first_rtcm_frame_timeout_ms = 5u;
    NtripClient client(config);
    ConfigureNonblockingReads(client);
    if (BeginStreaming(ctx, sockets, client, 1 * kSecond))
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      std::vector<std::uint8_t> buffer(64u, 0u);
      const auto timeout = client.Read(buffer.data(), buffer.size(), 31 * kSecond + 1);
      ctx.Expect(timeout.client_error == NtripClientError::kTimeout &&
                     client.state() == NtripClientState::kFailed,
                 "an accepted response with no first RTCM frame must time out and enter reconnect");
    }
  }

  {
    SocketPair sockets;
    NtripConfig config = MakeConfig();
    config.rtcm_frame_timeout_ms = 5u;
    NtripClient client(config);
    ConfigureNonblockingReads(client);
    const auto frame = BuildRtcmFrame(1077u);
    if (BeginStreaming(ctx, sockets, client, 1 * kSecond, frame))
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      std::vector<std::uint8_t> buffer(64u, 0u);
      const auto timeout = client.Read(buffer.data(), buffer.size(), 31 * kSecond + 1);
      ctx.Expect(timeout.client_error == NtripClientError::kTimeout &&
                     client.state() == NtripClientState::kFailed,
                 "a valid RTCM frame followed by inter-frame silence must time out");
    }
  }

  {
    SocketPair sockets;
    NtripConfig config = MakeConfig();
    config.first_rtcm_frame_timeout_ms = 5u;
    NtripClient client(config);
    ConfigureNonblockingReads(client);
    const std::vector<std::uint8_t> junk{'g', 'a', 'r', 'b', 'a', 'g', 'e'};
    if (BeginStreaming(ctx, sockets, client, 1 * kSecond, junk))
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      std::vector<std::uint8_t> buffer(64u, 0u);
      const auto timeout = client.Read(buffer.data(), buffer.size(), 31 * kSecond + 1);
      ctx.Expect(timeout.client_error == NtripClientError::kTimeout &&
                     client.correction_monitor().valid_frames() == 0u,
                 "arbitrary TCP payload must not keep correction flow alive");
    }
  }

  {
    SocketPair sockets;
    NtripConfig config = MakeConfig();
    config.first_rtcm_frame_timeout_ms = 100u;
    config.rtcm_frame_timeout_ms = 100u;
    NtripClient client(config);
    ConfigureNonblockingReads(client);
    const auto frame = BuildRtcmFrame(1077u);
    const std::size_t split = frame.size() / 2u;
    const auto split_offset = static_cast<std::ptrdiff_t>(split);
    const std::vector<std::uint8_t> prefix(frame.begin(), frame.begin() + split_offset);
    const std::vector<std::uint8_t> suffix(frame.begin() + split_offset, frame.end());
    if (BeginStreaming(ctx, sockets, client, 1 * kSecond, prefix))
    {
      ctx.Expect(client.correction_monitor().valid_frames() == 0u,
                 "a fragmented RTCM prefix must not count as correction flow");
      ctx.Expect(sockets.WritePeer(suffix), "fake caster should finish the fragmented RTCM frame");
      std::vector<std::uint8_t> buffer(64u, 0u);
      client.Read(buffer.data(), buffer.size(), 20 * kSecond);
      const auto below_timeout = client.Read(buffer.data(), buffer.size(), 49 * kSecond);
      ctx.Expect(client.correction_monitor().valid_frames() == 1u &&
                     below_timeout.client_error == NtripClientError::kNone &&
                     client.state() == NtripClientState::kStreaming,
                 "only the completed valid frame should refresh the inter-frame deadline");
    }
  }

  {
    SocketPair sockets;
    NtripConfig config = MakeConfig();
    config.rtcm_frame_timeout_ms = 20u;
    NtripClient client(config);
    ConfigureNonblockingReads(client);
    const auto frame = BuildRtcmFrame(1077u);
    if (BeginStreaming(ctx, sockets, client, 1 * kSecond, frame))
    {
      std::vector<std::uint8_t> buffer(64u, 0u);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      ctx.Expect(sockets.WritePeer(frame), "slow valid caster should send its next RTCM frame");
      client.Read(buffer.data(), buffer.size(), 21 * kSecond);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      ctx.Expect(sockets.WritePeer(frame), "slow valid caster should keep sending below timeout");
      client.Read(buffer.data(), buffer.size(), 41 * kSecond);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      const auto still_live = client.Read(buffer.data(), buffer.size(), 70 * kSecond);
      ctx.Expect(still_live.client_error == NtripClientError::kNone &&
                     client.state() == NtripClientState::kStreaming &&
                     client.correction_monitor().valid_frames() == 3u,
                 "regular low-rate valid RTCM below the configured deadline must not reconnect");
    }
  }

  {
    SocketPair sockets;
    NtripConfig config = MakeConfig();
    config.first_rtcm_frame_timeout_ms = 5u;
    NtripClient client(config);
    ConfigureNonblockingReads(client);
    if (BeginStreaming(ctx, sockets, client, 1 * kSecond))
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      std::vector<std::uint8_t> buffer(64u, 0u);
      const auto timeout = client.Read(buffer.data(), buffer.size());
      ctx.Expect(timeout.client_error == NtripClientError::kTimeout &&
                     client.state() == NtripClientState::kFailed,
                 "correction-flow deadlines must remain active when callers omit timestamps");
    }
  }
}

void TestCorrectionSourceOwnsStaticMetadataAcrossReconnect(TestContext& ctx)
{
  constexpr std::int64_t kSecond = 1000000000LL;
  NtripConfig config = MakeConfig();
  NtripClient client(config);
  ConfigureNonblockingReads(client);

  std::vector<std::uint8_t> station_a_stream = BuildRtcm1005Frame(23u);
  const auto station_a_msm = BuildRtcmMsmFrame(1077u, 23u, {1u}, {1u}, {true});
  station_a_stream.insert(station_a_stream.end(), station_a_msm.begin(), station_a_msm.end());

  SocketPair first_connection;
  if (!BeginStreaming(ctx, first_connection, client, 1 * kSecond, station_a_stream))
  {
    return;
  }
  ctx.Expect(client.correction_monitor().HasBaseStationPosition() &&
                 client.correction_monitor().MsmConstellationCount(
                     universal_gnss_protocols::RtcmConstellation::kGps) == 1u,
             "initial source session should decode station-owned base and dynamic RTCM state");

  SocketPair same_source_reconnect;
  ctx.Expect(same_source_reconnect.Open(), "same-source reconnect socketpair should open");
  ctx.Expect(client.AdoptConnectedSocket(same_source_reconnect.ReleaseClientFd()) ==
                 NtripClientError::kNone,
             "same-source reconnect should adopt the replacement transport");
  ctx.Expect(client.correction_monitor().HasBaseStationPosition(),
             "same-source reconnect should provisionally retain valid static 1005 metadata");
  ctx.Expect(client.correction_monitor().MsmConstellationCount(
                 universal_gnss_protocols::RtcmConstellation::kGps) == 0u,
             "same-source reconnect must always clear dynamic MSM freshness");

  ctx.Expect(client.SendRequest(2 * kSecond) == NtripClientError::kNone,
             "same-source reconnect should send a fresh request");
  same_source_reconnect.ReadPeerExact(client.request().request_text.size());
  std::vector<std::uint8_t> same_station_response{
      'I', 'C', 'Y', ' ', '2', '0', '0', ' ', 'O', 'K', '\r', '\n', '\r', '\n'};
  same_station_response.insert(
      same_station_response.end(), station_a_msm.begin(), station_a_msm.end());
  ctx.Expect(same_source_reconnect.WritePeer(same_station_response),
             "same source/station should resume with fresh MSM");
  std::vector<std::uint8_t> buffer(4096u, 0u);
  client.Read(buffer.data(), buffer.size(), 2 * kSecond);

  universal_gnss_protocols::RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 2 * kSecond;
  options.stale_after_ns = 5 * kSecond;
  options.required_observation_window_ns = 30 * kSecond;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);
  ctx.Expect(client.correction_monitor().HasRequiredCorrectionMessages(options),
             "same source/station reconnect should combine retained static metadata only with new MSM");

  const auto station_b_msm = BuildRtcmMsmFrame(1077u, 24u, {1u}, {1u}, {true});
  ctx.Expect(same_source_reconnect.WritePeer(station_b_msm),
             "same endpoint may later reveal a different RTCM station");
  client.Read(buffer.data(), buffer.size(), 3 * kSecond);
  ctx.Expect(!client.correction_monitor().HasBaseStationPosition() &&
                 !client.correction_monitor().HasRequiredCorrectionMessages(options),
             "a different post-reconnect station must invalidate provisionally retained metadata");

  NtripClient changed_source_client(config);
  ConfigureNonblockingReads(changed_source_client);
  SocketPair original_source;
  if (!BeginStreaming(ctx, original_source, changed_source_client, 1 * kSecond,
                      BuildRtcm1005Frame(23u)))
  {
    return;
  }
  NtripConfig normalized_same_source = config;
  normalized_same_source.host = "CASTER.EXAMPLE.COM";
  normalized_same_source.mountpoint = "/RTCM32";
  changed_source_client.set_config(normalized_same_source);
  ctx.Expect(changed_source_client.state() == NtripClientState::kStreaming &&
                 changed_source_client.correction_monitor().HasBaseStationPosition(),
             "host case and equivalent mountpoint path must preserve the same source identity");

  NtripConfig changed_source = config;
  changed_source.mountpoint = "OTHER";
  changed_source_client.set_config(changed_source);
  ctx.Expect(changed_source_client.state() == NtripClientState::kDisconnected &&
                 !changed_source_client.correction_monitor().HasBaseStationPosition(),
             "explicit mountpoint/source change must close the session and clear old static metadata");

  NtripClient changed_port_client(config);
  ConfigureNonblockingReads(changed_port_client);
  SocketPair original_port_source;
  if (!BeginStreaming(ctx, original_port_source, changed_port_client, 1 * kSecond,
                      BuildRtcm1005Frame(23u)))
  {
    return;
  }
  NtripConfig changed_port = config;
  changed_port.port = 2201u;
  changed_port_client.set_config(changed_port);
  ctx.Expect(changed_port_client.state() == NtripClientState::kDisconnected &&
                 !changed_port_client.correction_monitor().HasBaseStationPosition(),
             "explicit caster port change must close the session and clear source-owned metadata");

  NtripClient changed_host_client(config);
  ConfigureNonblockingReads(changed_host_client);
  SocketPair original_host_source;
  if (!BeginStreaming(ctx, original_host_source, changed_host_client, 1 * kSecond,
                      BuildRtcm1005Frame(23u)))
  {
    return;
  }
  NtripConfig changed_host = config;
  changed_host.host = "other-caster.example.com";
  changed_host_client.set_config(changed_host);
  ctx.Expect(changed_host_client.state() == NtripClientState::kDisconnected &&
                 !changed_host_client.correction_monitor().HasBaseStationPosition(),
             "explicit caster host change must close the session and clear source-owned metadata");
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

void TestExplicitStreamingOnlyGgaInjection(TestContext& ctx)
{
  {
    NtripConfig config = MakeConfig();
    config.send_gga = true;
    NtripClient client(config);

    const auto inject_result = client.MaybeInjectGga(MakeRuntimeState(), 1000000000LL);
    ctx.Expect(inject_result.status == NtripGgaSendStatus::kSkippedNotStreaming &&
                   inject_result.skipped() &&
                   client.gga_metrics().attempts == 0u &&
                   client.metrics().gga_sent_count == 0u,
               "MaybeInjectGga should no-op cleanly before the client reaches the streaming state");
  }

  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for streaming-only GGA injection test");

    NtripConfig config = MakeConfig();
    config.send_gga = true;
    config.gga_interval_s = 5u;
    NtripClient client(config);
    universal_gnss_transport::TcpClientConfig tcp_config;
    tcp_config.read_timeout_ms = 50u;
    client.set_tcp_config(tcp_config);

    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone,
               "adopting a connected socket should succeed before explicit GGA injection");
    ctx.Expect(client.SendRequest() == NtripClientError::kNone,
               "sending the request should succeed before explicit GGA injection");
    sockets.ReadPeerExact(client.request().request_text.size());

    ctx.Expect(sockets.WritePeer("ICY 200 OK\r\nNtrip-Version: Ntrip/2.0\r\n\r\n"),
               "the fake peer should send a valid response header before GGA injection");
    std::vector<std::uint8_t> buffer(64u, 0u);
    universal_gnss_ntrip::NtripClientReadResult read_result;
    for (int attempt = 0; attempt < 4 && client.state() != NtripClientState::kStreaming; ++attempt)
    {
      read_result = client.Read(buffer.data(), buffer.size(), 1000000000LL + attempt);
      if (read_result.client_error != NtripClientError::kNone)
      {
        break;
      }
    }
    ctx.Expect(read_result.client_error == NtripClientError::kNone &&
                   client.state() == NtripClientState::kStreaming,
               "the client should enter streaming before explicit GGA injection");

    const std::uint64_t bytes_sent_before = client.metrics().bytes_sent;
    const auto expected_sentence =
        universal_gnss_ntrip::GenerateGgaFromRuntimeState(MakeRuntimeState()).sentence;
    const auto first_inject = client.MaybeInjectGga(MakeRuntimeState(), 2000000000LL);
    ctx.Expect(first_inject.status == NtripGgaSendStatus::kSent &&
                   client.gga_metrics().attempts == 1u &&
                   client.gga_metrics().sentences_built == 1u &&
                   client.gga_metrics().sentences_sent == 1u &&
                   client.metrics().gga_sent_count == 1u &&
                   client.metrics().bytes_sent == bytes_sent_before + expected_sentence.size() &&
                   client.gga_injection_policy().last_sent_timestamp_ns ==
                       std::optional<std::int64_t>(2000000000LL),
               "MaybeInjectGga should send once streaming, update injector metrics, and reflect bytes in the client metrics");

    const auto peer_bytes = sockets.ReadPeerExact(expected_sentence.size());
    const std::string peer_text(peer_bytes.begin(), peer_bytes.end());
    ctx.Expect(peer_text == expected_sentence,
               "MaybeInjectGga should write the exact generated GGA sentence to the transport");

    const auto gated_inject = client.MaybeInjectGga(MakeRuntimeState(), 4000000000LL);
    ctx.Expect(gated_inject.status == NtripGgaSendStatus::kSkippedInterval &&
                   client.gga_metrics().attempts == 2u &&
                   client.gga_metrics().skipped_interval == 1u &&
                   client.metrics().gga_sent_count == 1u,
               "MaybeInjectGga should gate repeated sends with the configured interval");
  }

  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for missing-position explicit GGA test");

    NtripConfig config = MakeConfig();
    config.send_gga = true;
    NtripClient client(config);
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone &&
                   client.SendRequest() == NtripClientError::kNone,
               "setup should succeed before missing-position explicit GGA test");
    sockets.ReadPeerExact(client.request().request_text.size());
    ctx.Expect(sockets.WritePeer("ICY 200 OK\r\n\r\n"),
               "the fake peer should send a valid response header");
    std::vector<std::uint8_t> buffer(16u, 0u);
    client.Read(buffer.data(), buffer.size(), 1000000000LL);

    auto missing_position = MakeRuntimeState();
    missing_position.longitude_deg.reset();
    const auto result = client.MaybeInjectGga(missing_position, 2000000000LL);
    ctx.Expect(result.status == NtripGgaSendStatus::kSkippedMissingPosition &&
                   client.gga_metrics().skipped_missing_position == 1u &&
                   client.metrics().gga_sent_count == 0u,
               "MaybeInjectGga should skip cleanly when coordinates are missing");
  }

  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for required-fix explicit GGA test");

    NtripConfig config = MakeConfig();
    config.send_gga = true;
    NtripClient client(config);
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone &&
                   client.SendRequest() == NtripClientError::kNone,
               "setup should succeed before required-fix explicit GGA test");
    sockets.ReadPeerExact(client.request().request_text.size());
    ctx.Expect(sockets.WritePeer("ICY 200 OK\r\n\r\n"),
               "the fake peer should send a valid response header");
    std::vector<std::uint8_t> buffer(16u, 0u);
    client.Read(buffer.data(), buffer.size(), 1000000000LL);

    auto no_fix_state = MakeRuntimeState();
    no_fix_state.fix_valid = false;
    no_fix_state.fix_type = GnssFixType::kNoFix;
    const auto result = client.MaybeInjectGga(no_fix_state, 2000000000LL);
    ctx.Expect(result.status == NtripGgaSendStatus::kSkippedPositionRequired &&
                   client.gga_metrics().skipped_position_required == 1u &&
                   client.metrics().gga_sent_count == 0u,
               "MaybeInjectGga should skip when the configured policy requires a valid fix");
  }

  {
    SocketPair sockets;
    ctx.Expect(sockets.Open(), "socketpair fixture should open for write-failure explicit GGA test");

    NtripConfig config = MakeConfig();
    config.send_gga = true;
    NtripClient client(config);
    ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd()) == NtripClientError::kNone &&
                   client.SendRequest() == NtripClientError::kNone,
               "setup should succeed before explicit GGA write-failure test");
    sockets.ReadPeerExact(client.request().request_text.size());
    ctx.Expect(sockets.WritePeer("ICY 200 OK\r\n\r\n"),
               "the fake peer should send a valid response header");
    std::vector<std::uint8_t> buffer(16u, 0u);
    client.Read(buffer.data(), buffer.size(), 1000000000LL);

    const auto previous_handler = std::signal(SIGPIPE, SIG_IGN);
    sockets.ClosePeer();
    const auto result = client.MaybeInjectGga(MakeRuntimeState(), 2000000000LL);
    std::signal(SIGPIPE, previous_handler);

    ctx.Expect(result.status == NtripGgaSendStatus::kError &&
                   result.client_error == NtripClientError::kDisconnected &&
                   result.send_error ==
                       std::optional<NtripGgaSendError>(NtripGgaSendError::kWriteFailure) &&
                   client.gga_metrics().write_errors == 1u &&
                   client.metrics().gga_send_errors == 1u &&
                   !client.gga_injection_policy().last_sent_timestamp_ns.has_value() &&
                   client.state() == NtripClientState::kFailed,
               "explicit GGA write failures should update both injector and client metrics without advancing last-sent time");
  }
}

}  // namespace

int main()
{
  TestContext ctx;

  TestRequestAndStreamingFlow(ctx);
  TestSplitHttpResponseAndDisconnect(ctx);
  TestLegacyIcyResponseWithoutBlankLine(ctx);
  TestSplitLegacyIcyResponseWithoutBlankLine(ctx);
  TestLegacyIcyResponseWithMidFrameBinary(ctx);
  TestNtripStatusCodeTokenValidation(ctx);
  TestInvalidResponsesAndConnectFailure(ctx);
  TestCorrectionFlowLivenessUsesCompleteValidFrames(ctx);
  TestCorrectionSourceOwnsStaticMetadataAcrossReconnect(ctx);
  TestExplicitAndPolicyDrivenGgaSending(ctx);
  TestExplicitStreamingOnlyGgaInjection(ctx);

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
