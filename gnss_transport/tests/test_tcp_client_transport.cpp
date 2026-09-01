#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#if defined(__linux__)

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "universal_gnss_transport/tcp_client_transport.hpp"

namespace
{

using universal_gnss_transport::TcpClientConfig;
using universal_gnss_transport::TcpClientTransport;
using universal_gnss_transport::TransportError;
using universal_gnss_transport::TransportStatus;

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

std::string ToString(const universal_gnss_transport::TransportError error)
{
  return std::to_string(static_cast<int>(error));
}

std::string ToString(const universal_gnss_transport::TransportStatus status)
{
  return std::to_string(static_cast<int>(status));
}

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

bool IsClosedPeerWriteFailure(const universal_gnss_transport::WriteResult& result,
                              const TcpClientTransport& client)
{
  return result.bytes_written == 0u && result.status == TransportStatus::kError &&
         result.error == TransportError::kWriteFailure && client.metrics().write_errors == 1u &&
         client.metrics().last_error == TransportError::kWriteFailure;
}

int RunAdoptedClosedPeerWriteChild()
{
  std::signal(SIGPIPE, SIG_DFL);

  int fds[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
  {
    return 10;
  }

  TcpClientTransport client;
  TcpClientConfig config;
  config.nonblocking = true;
  if (client.AdoptConnectedSocket(fds[0], config) != TransportError::kNone)
  {
    ::close(fds[0]);
    ::close(fds[1]);
    return 11;
  }

  ::shutdown(fds[1], SHUT_RDWR);
  ::close(fds[1]);
  const std::uint8_t byte = 0xA5u;
  return IsClosedPeerWriteFailure(client.Write(&byte, 1u), client) ? 0 : 12;
}

int CreateLoopbackListener(std::uint16_t& port)
{
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0)
  {
    return -1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(listener, 1) != 0)
  {
    ::close(listener);
    return -1;
  }

  socklen_t address_size = sizeof(address);
  if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_size) != 0)
  {
    ::close(listener);
    return -1;
  }

  port = ntohs(address.sin_port);
  return listener;
}

int RunNormallyOpenedClosedPeerWriteChild(const std::uint16_t port,
                                          const int opened_pipe,
                                          const int close_pipe)
{
  std::signal(SIGPIPE, SIG_DFL);

  TcpClientTransport client;
  TcpClientConfig config;
  config.host = "127.0.0.1";
  config.port = port;
  config.nonblocking = true;
  if (client.Open(config) != TransportError::kNone)
  {
    return 20;
  }

  const std::uint8_t ready = 1u;
  if (::write(opened_pipe, &ready, 1u) != 1)
  {
    return 21;
  }
  std::uint8_t peer_closed = 0u;
  if (::read(close_pipe, &peer_closed, 1u) != 1)
  {
    return 22;
  }

  pollfd descriptor{};
  descriptor.fd = client.native_fd();
  descriptor.events = POLLIN;
  if (::poll(&descriptor, 1, 1000) <= 0 ||
      (descriptor.revents & (POLLIN | POLLERR | POLLHUP)) == 0)
  {
    return 23;
  }
  std::uint8_t received = 0u;
  const auto closed_read = client.Read(&received, 1u);
  if (closed_read.status != TransportStatus::kEndOfStream &&
      closed_read.status != TransportStatus::kError)
  {
    return 24;
  }

  const std::uint8_t byte = 0x5Au;
  return IsClosedPeerWriteFailure(client.Write(&byte, 1u), client) ? 0 : 25;
}

bool ChildExitedSuccessfully(const pid_t child, int& child_status)
{
  if (::waitpid(child, &child_status, 0) != child)
  {
    return false;
  }
  return WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0;
}

void TestClosedPeerWritesDoNotRaiseSigpipe(TestContext& ctx)
{
  const pid_t adopted_child = ::fork();
  if (adopted_child == 0)
  {
    _exit(RunAdoptedClosedPeerWriteChild());
  }
  int adopted_status = 0;
  const bool adopted_succeeded =
      adopted_child > 0 && ChildExitedSuccessfully(adopted_child, adopted_status);
  ctx.Expect(adopted_succeeded,
             "adopted closed-peer write must survive default SIGPIPE and return kWriteFailure "
             "(wait status=" + std::to_string(adopted_status) + ")");

  std::uint16_t port = 0u;
  const int listener = CreateLoopbackListener(port);
  if (listener < 0)
  {
    std::cout << "Normal TCP closed-peer SIGPIPE regression skipped: loopback socket unavailable\n";
    return;
  }

  int opened_pipe[2] = {-1, -1};
  int close_pipe[2] = {-1, -1};
  if (::pipe(opened_pipe) != 0 || ::pipe(close_pipe) != 0)
  {
    if (opened_pipe[0] >= 0)
    {
      ::close(opened_pipe[0]);
      ::close(opened_pipe[1]);
    }
    if (close_pipe[0] >= 0)
    {
      ::close(close_pipe[0]);
      ::close(close_pipe[1]);
    }
    ::close(listener);
    ctx.Expect(false, "SIGPIPE test synchronization pipes should open");
    return;
  }

  const pid_t normal_child = ::fork();
  if (normal_child == 0)
  {
    ::close(opened_pipe[0]);
    ::close(close_pipe[1]);
    ::close(listener);
    _exit(RunNormallyOpenedClosedPeerWriteChild(port, opened_pipe[1], close_pipe[0]));
  }

  ::close(opened_pipe[1]);
  ::close(close_pipe[0]);
  std::uint8_t opened = 0u;
  const bool child_opened = normal_child > 0 && ::read(opened_pipe[0], &opened, 1u) == 1;
  const int accepted = child_opened ? ::accept(listener, nullptr, nullptr) : -1;
  if (accepted >= 0)
  {
    linger abortive_close{};
    abortive_close.l_onoff = 1;
    abortive_close.l_linger = 0;
    ::setsockopt(accepted, SOL_SOCKET, SO_LINGER, &abortive_close, sizeof(abortive_close));
    ::close(accepted);
  }
  const std::uint8_t peer_closed = 1u;
  const bool notified_child = child_opened &&
                              ::write(close_pipe[1], &peer_closed, 1u) == 1;
  ::close(opened_pipe[0]);
  ::close(close_pipe[1]);
  ::close(listener);

  int normal_status = 0;
  const bool normal_succeeded = normal_child > 0 &&
                                ChildExitedSuccessfully(normal_child, normal_status);
  ctx.Expect(child_opened && accepted >= 0 && notified_child && normal_succeeded,
             "normally opened closed-peer write must survive default SIGPIPE and return kWriteFailure "
             "(wait status=" + std::to_string(normal_status) + ")");
}

void TestOpenReadWriteCloseAndMetrics(TestContext& ctx)
{
  const std::vector<std::uint8_t> inbound = {0x10u, 0x20u, 0x30u};
  const std::vector<std::uint8_t> outbound = {0xAAu, 0xBBu};

  SocketPair sockets;
  ctx.Expect(sockets.Open(), "socketpair fixture should open");

  TcpClientTransport client;
  TcpClientConfig config;
  config.read_timeout_ms = 200u;
  config.write_timeout_ms = 200u;

  const auto open_error = client.AdoptConnectedSocket(sockets.ReleaseClientFd(), config);
  ctx.Expect(
      open_error == TransportError::kNone && client.IsOpen(),
      "TCP client should adopt an already-connected stream socket (error=" + ToString(open_error) +
          ", is_open=" + std::to_string(client.IsOpen()) + ")");

  ctx.Expect(sockets.WritePeer(inbound), "socketpair peer should accept inbound data");
  std::vector<std::uint8_t> read_buffer(inbound.size(), 0u);
  const auto read_result = client.Read(read_buffer.data(), read_buffer.size());
  ctx.Expect(
      read_result.status == TransportStatus::kOk && read_result.bytes_read == inbound.size() &&
          read_buffer == inbound,
      "TCP client should read bytes sent by the socketpair peer (status=" +
          ToString(read_result.status) + ", error=" + ToString(read_result.error) +
          ", bytes=" + std::to_string(read_result.bytes_read) + ")");

  const auto write_result = client.Write(outbound.data(), outbound.size());
  const bool write_succeeded =
      write_result.status == TransportStatus::kOk && write_result.bytes_written == outbound.size();
  ctx.Expect(
      write_succeeded,
      "TCP client should write bytes to the socketpair peer (status=" +
          ToString(write_result.status) + ", error=" + ToString(write_result.error) +
          ", bytes=" + std::to_string(write_result.bytes_written) + ")");
  ctx.Expect(client.metrics().bytes_read == inbound.size() &&
                 client.metrics().bytes_written == outbound.size() &&
                 client.metrics().read_errors == 0u &&
                 client.metrics().write_errors == 0u,
             "TCP client should update metrics for successful I/O");

  const std::vector<std::uint8_t> outbound_received =
      write_succeeded ? sockets.ReadPeerExact(outbound.size()) : std::vector<std::uint8_t>{};
  client.Close();
  ctx.Expect(!client.IsOpen(), "TCP client close should release the socket");
  ctx.Expect(write_succeeded && outbound_received == outbound,
             "socketpair peer should receive bytes written by the client");
}

void TestReadTimeoutAndNonblockingBehavior(TestContext& ctx)
{
  SocketPair timeout_sockets;
  ctx.Expect(timeout_sockets.Open(), "timeout socketpair fixture should open");

  TcpClientTransport timeout_client;
  TcpClientConfig timeout_config;
  timeout_config.read_timeout_ms = 50u;

  ctx.Expect(timeout_client.AdoptConnectedSocket(timeout_sockets.ReleaseClientFd(), timeout_config) ==
                 TransportError::kNone,
             "TCP client should adopt a socket for timeout test");

  std::vector<std::uint8_t> buffer(4u, 0u);
  const auto timed_read = timeout_client.Read(buffer.data(), buffer.size());
  ctx.Expect(
      timed_read.status == TransportStatus::kOk && timed_read.bytes_read == 0u &&
          timed_read.error == TransportError::kNone,
      "read timeout without data should return zero bytes without transport error (status=" +
          ToString(timed_read.status) + ", error=" + ToString(timed_read.error) +
          ", bytes=" + std::to_string(timed_read.bytes_read) + ")");

  SocketPair nonblocking_sockets;
  ctx.Expect(nonblocking_sockets.Open(), "nonblocking socketpair fixture should open");

  TcpClientTransport nonblocking_client;
  TcpClientConfig nonblocking_config;
  nonblocking_config.nonblocking = true;

  ctx.Expect(
      nonblocking_client.AdoptConnectedSocket(nonblocking_sockets.ReleaseClientFd(), nonblocking_config) ==
          TransportError::kNone,
      "TCP client should adopt a socket in nonblocking mode");

  const auto nonblocking_read = nonblocking_client.Read(buffer.data(), buffer.size());
  ctx.Expect(nonblocking_read.status == TransportStatus::kOk &&
                 nonblocking_read.bytes_read == 0u &&
                 nonblocking_read.error == TransportError::kNone,
             "nonblocking read without data should return zero bytes without transport error");
}

void TestConnectFailureAndInvalidConfiguration(TestContext& ctx)
{
  TcpClientTransport client;

  TcpClientConfig invalid_config;
  invalid_config.port = 2101u;
  ctx.Expect(client.Open(invalid_config) == TransportError::kInvalidArgument &&
                 client.metrics().last_error == TransportError::kInvalidArgument,
             "empty host should be rejected");

  invalid_config.host = "127.0.0.1";
  invalid_config.port = 0u;
  ctx.Expect(client.Open(invalid_config) == TransportError::kInvalidArgument,
             "zero port should be rejected");

  TcpClientConfig connect_failure_config;
  connect_failure_config.host = "256.256.256.256";
  connect_failure_config.port = 2101u;
  connect_failure_config.connect_timeout_ms = 100u;
  const auto connect_error = client.Open(connect_failure_config);
  ctx.Expect(connect_error == TransportError::kConnectFailure &&
                 client.metrics().last_error == TransportError::kConnectFailure &&
                 !client.IsOpen(),
             "invalid TCP host resolution should fail cleanly");
}

void TestClosedReadWriteBehavior(TestContext& ctx)
{
  SocketPair sockets;
  ctx.Expect(sockets.Open(), "socketpair fixture should open for close-behavior test");

  TcpClientTransport client;
  TcpClientConfig config;
  ctx.Expect(client.AdoptConnectedSocket(sockets.ReleaseClientFd(), config) == TransportError::kNone,
             "TCP client should adopt a socket before close-behavior test");

  client.Close();

  std::vector<std::uint8_t> buffer(1u, 0u);
  const std::vector<std::uint8_t> outbound = {0x55u};
  const auto read_result = client.Read(buffer.data(), buffer.size());
  const auto write_result = client.Write(outbound.data(), outbound.size());
  ctx.Expect(read_result.status == TransportStatus::kClosed &&
                 read_result.error == TransportError::kClosed &&
                 write_result.status == TransportStatus::kClosed &&
                 write_result.error == TransportError::kClosed,
             "closed TCP client should reject reads and writes");
  ctx.Expect(client.metrics().read_errors == 1u &&
                 client.metrics().write_errors == 1u &&
                 client.metrics().last_error == TransportError::kClosed,
             "closed-direction calls should update transport metrics");
}

void TestTlsConfigurationAndHandshakeFailure(TestContext& ctx)
{
  SocketPair nonblocking_sockets;
  ctx.Expect(nonblocking_sockets.Open(), "socketpair fixture should open for TLS mode test");

  TcpClientTransport nonblocking_client;
  TcpClientConfig nonblocking_config;
  nonblocking_config.host = "localhost";
  nonblocking_config.tls_enabled = true;
  nonblocking_config.nonblocking = true;
  ctx.Expect(
      nonblocking_client.AdoptConnectedSocket(
          nonblocking_sockets.ReleaseClientFd(), nonblocking_config) == TransportError::kUnsupported &&
          !nonblocking_client.IsOpen(),
      "TLS transport should reject unsupported nonblocking handshakes without retaining the socket");

  SocketPair closed_peer_sockets;
  ctx.Expect(closed_peer_sockets.Open(), "socketpair fixture should open for TLS handshake failure test");
  closed_peer_sockets.ClosePeer();

  TcpClientTransport client;
  TcpClientConfig config;
  config.host = "localhost";
  config.tls_enabled = true;
  config.tls_verify_peer = false;
  ctx.Expect(
      client.AdoptConnectedSocket(closed_peer_sockets.ReleaseClientFd(), config) ==
              TransportError::kTlsHandshakeFailure &&
          !client.IsOpen(),
      "interrupted TLS handshakes should fail and close the transport");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestOpenReadWriteCloseAndMetrics(ctx);
  TestReadTimeoutAndNonblockingBehavior(ctx);
  TestConnectFailureAndInvalidConfiguration(ctx);
  TestClosedReadWriteBehavior(ctx);
  TestTlsConfigurationAndHandshakeFailure(ctx);
  TestClosedPeerWritesDoNotRaiseSigpipe(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_transport TCP client tests passed\n";
  return EXIT_SUCCESS;
}

#else

int main()
{
  std::cout << "TCP client transport tests skipped on non-Linux platforms\n";
  return EXIT_SUCCESS;
}

#endif
