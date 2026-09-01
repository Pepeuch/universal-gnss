#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(__linux__)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "universal_gnss_transport/udp_client_transport.hpp"

namespace
{
using universal_gnss_transport::TransportError;
using universal_gnss_transport::TransportStatus;
using universal_gnss_transport::UdpClientConfig;
using universal_gnss_transport::UdpClientTransport;

struct TestContext
{
  int failures{0};
  void Expect(bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

class LoopbackUdpServer
{
public:
  ~LoopbackUdpServer()
  {
    if (fd_ >= 0)
    {
      ::close(fd_);
    }
  }

  bool Open()
  {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd_ < 0 || ::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
      return false;
    }
    socklen_t size = sizeof(address);
    return ::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &size) == 0 &&
           (port_ = ntohs(address.sin_port)) != 0u;
  }

  std::uint16_t port() const { return port_; }

  bool Receive(std::vector<std::uint8_t>& data)
  {
    pollfd descriptor{};
    descriptor.fd = fd_;
    descriptor.events = POLLIN;
    if (::poll(&descriptor, 1, 250) <= 0 || (descriptor.revents & POLLIN) == 0)
    {
      return false;
    }
    std::uint8_t buffer[256]{};
    peer_size_ = sizeof(peer_);
    const ssize_t received = ::recvfrom(fd_, buffer, sizeof(buffer), 0,
                                        reinterpret_cast<sockaddr*>(&peer_), &peer_size_);
    if (received < 0)
    {
      return false;
    }
    data.assign(buffer, buffer + received);
    return true;
  }

  bool Send(const std::vector<std::uint8_t>& data) const
  {
    return peer_size_ > 0u &&
           ::sendto(fd_, data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&peer_),
                    peer_size_) == static_cast<ssize_t>(data.size());
  }

private:
  int fd_{-1};
  std::uint16_t port_{0u};
  sockaddr_storage peer_{};
  socklen_t peer_size_{0u};
};

void OpenClient(TestContext& ctx,
                UdpClientTransport& client,
                LoopbackUdpServer& server,
                UdpClientConfig config = {})
{
  config.host = "127.0.0.1";
  config.port = server.port();
  ctx.Expect(client.Open(config) == TransportError::kNone && client.IsOpen(),
             "UDP client should connect to loopback server");
}

void EstablishPeer(TestContext& ctx, UdpClientTransport& client, LoopbackUdpServer& server)
{
  const std::uint8_t probe = 0u;
  std::vector<std::uint8_t> received;
  ctx.Expect(client.Write(&probe, 1u).status == TransportStatus::kOk && server.Receive(received),
             "UDP loopback peer should be established by one datagram");
}

void TestDatagramRoundTrip(TestContext& ctx)
{
  LoopbackUdpServer server;
  if (!server.Open())
  {
    std::cout << "UDP loopback test skipped: local UDP bind unavailable\n";
    return;
  }
  UdpClientConfig config;
  config.read_timeout_ms = 250u;
  UdpClientTransport client;
  OpenClient(ctx, client, server, config);
  const std::vector<std::uint8_t> outbound = {0xA1u, 0xB2u};
  const auto write = client.Write(outbound.data(), outbound.size());
  std::vector<std::uint8_t> received;
  ctx.Expect(write.status == TransportStatus::kOk && write.bytes_written == outbound.size() &&
                 server.Receive(received) && received == outbound,
             "one UDP write should produce one datagram");
  const std::vector<std::uint8_t> inbound = {0x10u, 0x20u, 0x30u};
  ctx.Expect(server.Send(inbound), "loopback server should send one datagram");
  std::uint8_t buffer[8]{};
  const auto read = client.Read(buffer, sizeof(buffer));
  ctx.Expect(read.status == TransportStatus::kOk && read.bytes_read == inbound.size() &&
                 std::memcmp(buffer, inbound.data(), inbound.size()) == 0 &&
                 client.metrics().bytes_read == inbound.size() &&
                 client.metrics().bytes_written == outbound.size(),
             "one UDP read should return one complete datagram and count bytes");
}

void TestBoundaryAndTruncation(TestContext& ctx)
{
  LoopbackUdpServer server;
  if (!server.Open())
  {
    std::cout << "UDP loopback boundary test skipped: local UDP bind unavailable\n";
    return;
  }
  UdpClientConfig config;
  config.read_timeout_ms = 250u;
  UdpClientTransport client;
  OpenClient(ctx, client, server, config);
  EstablishPeer(ctx, client, server);
  const std::vector<std::uint8_t> first = {0x01u, 0x02u};
  const std::vector<std::uint8_t> oversized = {0x11u, 0x12u, 0x13u};
  const std::vector<std::uint8_t> next = {0x21u};
  ctx.Expect(server.Send(first) && server.Send(oversized) && server.Send(next),
             "server should queue loopback datagrams");
  const auto zero = client.Read(nullptr, 0u);
  std::uint8_t buffer[2]{};
  const auto first_read = client.Read(buffer, sizeof(buffer));
  const bool first_matches = first_read.status == TransportStatus::kOk &&
                             first_read.bytes_read == first.size() &&
                             std::memcmp(buffer, first.data(), first.size()) == 0;
  const auto overflow = client.Read(buffer, sizeof(buffer));
  const auto next_read = client.Read(buffer, sizeof(buffer));
  const bool next_matches = next_read.status == TransportStatus::kOk &&
                            next_read.bytes_read == next.size() && buffer[0] == next[0];
  ctx.Expect(zero.status == TransportStatus::kOk && zero.bytes_read == 0u && first_matches &&
                 overflow.status == TransportStatus::kError &&
                 overflow.error == TransportError::kOverflow && overflow.bytes_read == 0u &&
                 next_matches && client.metrics().read_errors == 1u,
             "zero-capacity preserves datagram; oversized datagram is discarded before next read");
}

void TestTimeoutNonblockingAndClose(TestContext& ctx)
{
  LoopbackUdpServer server;
  if (!server.Open())
  {
    std::cout << "UDP loopback read-mode test skipped: local UDP bind unavailable\n";
    return;
  }
  UdpClientConfig timeout_config;
  timeout_config.read_timeout_ms = 25u;
  UdpClientTransport timeout_client;
  OpenClient(ctx, timeout_client, server, timeout_config);
  std::uint8_t byte = 0u;
  const auto timed = timeout_client.Read(&byte, 1u);
  ctx.Expect(timed.status == TransportStatus::kOk && timed.bytes_read == 0u &&
                 timed.error == TransportError::kNone,
             "UDP timeout should return an empty successful read");
  UdpClientConfig nonblocking_config;
  nonblocking_config.nonblocking = true;
  UdpClientTransport nonblocking_client;
  OpenClient(ctx, nonblocking_client, server, nonblocking_config);
  const auto nonblocking = nonblocking_client.Read(&byte, 1u);
  ctx.Expect(nonblocking.status == TransportStatus::kOk && nonblocking.bytes_read == 0u &&
                 nonblocking.error == TransportError::kNone,
             "nonblocking UDP read should return empty when no datagram is queued");
  timeout_client.Close();
  const auto closed_read = timeout_client.Read(&byte, 1u);
  const auto closed_write = timeout_client.Write(&byte, 1u);
  ctx.Expect(closed_read.status == TransportStatus::kClosed &&
                 closed_write.status == TransportStatus::kClosed &&
                 timeout_client.metrics().read_errors == 1u &&
                 timeout_client.metrics().write_errors == 1u,
             "closed UDP directions should fail and increment their metrics");
}
}  // namespace

int main()
{
  TestContext ctx;
  TestDatagramRoundTrip(ctx);
  TestBoundaryAndTruncation(ctx);
  TestTimeoutNonblockingAndClose(ctx);
  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All gnss_transport UDP client tests passed\n";
  return EXIT_SUCCESS;
}

#else

int main()
{
  return EXIT_SUCCESS;
}

#endif
