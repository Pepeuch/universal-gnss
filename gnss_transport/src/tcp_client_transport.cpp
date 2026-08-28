#include "universal_gnss_transport/tcp_client_transport.hpp"

#if defined(__linux__)

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace universal_gnss_transport
{

namespace
{

bool SetSocketNonBlocking(const int fd, const bool nonblocking)
{
  const int current_flags = ::fcntl(fd, F_GETFL, 0);
  if (current_flags < 0)
  {
    return false;
  }

  int updated_flags = current_flags;
  if (nonblocking)
  {
    updated_flags |= O_NONBLOCK;
  }
  else
  {
    updated_flags &= ~O_NONBLOCK;
  }

  return ::fcntl(fd, F_SETFL, updated_flags) == 0;
}

bool SetCloseOnExec(const int fd)
{
  const int current_flags = ::fcntl(fd, F_GETFD, 0);
  if (current_flags < 0)
  {
    return false;
  }

  return ::fcntl(fd, F_SETFD, current_flags | FD_CLOEXEC) == 0;
}

TransportError ConfigureConnectedSocket(const int fd, const TcpClientConfig& config)
{
  if (config.tcp_nodelay)
  {
    const int flag = 1;
    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) != 0)
    {
      return TransportError::kUnsupported;
    }
  }

  if (!SetSocketNonBlocking(fd, config.nonblocking))
  {
    return TransportError::kUnknown;
  }

  if (!SetCloseOnExec(fd))
  {
    return TransportError::kUnknown;
  }

  return TransportError::kNone;
}

enum class WaitForSocketResult : std::uint8_t
{
  kReady = 0,
  kTimeout = 1,
  kError = 2,
};

WaitForSocketResult WaitForSocketEvent(const int fd,
                                      const short requested_events,
                                      const std::uint32_t timeout_ms)
{
  if (timeout_ms == 0u)
  {
    return WaitForSocketResult::kReady;
  }

  pollfd descriptor{};
  descriptor.fd = fd;
  descriptor.events = requested_events;

  for (;;)
  {
    const int timeout_for_poll =
        timeout_ms > static_cast<std::uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(timeout_ms);
    const int poll_result = ::poll(&descriptor, 1, timeout_for_poll);
    if (poll_result > 0)
    {
      if ((descriptor.revents & POLLNVAL) != 0)
      {
        return WaitForSocketResult::kError;
      }

      if ((descriptor.revents & requested_events) != 0 ||
          ((descriptor.revents & POLLHUP) != 0 && (requested_events & POLLIN) != 0))
      {
        return WaitForSocketResult::kReady;
      }

      if ((descriptor.revents & (POLLERR | POLLHUP)) != 0)
      {
        return WaitForSocketResult::kError;
      }

      return WaitForSocketResult::kError;
    }

    if (poll_result == 0)
    {
      return WaitForSocketResult::kTimeout;
    }

    if (errno == EINTR)
    {
      continue;
    }

    return WaitForSocketResult::kError;
  }
}

TransportError ConnectWithTimeout(const int fd,
                                  const sockaddr* address,
                                  const socklen_t address_length,
                                  const std::uint32_t timeout_ms)
{
  if (timeout_ms == 0u)
  {
    for (;;)
    {
      if (::connect(fd, address, address_length) == 0)
      {
        return TransportError::kNone;
      }

      if (errno == EINTR)
      {
        continue;
      }

      return TransportError::kConnectFailure;
    }
  }

  if (!SetSocketNonBlocking(fd, true))
  {
    return TransportError::kUnknown;
  }

  if (::connect(fd, address, address_length) == 0)
  {
    return TransportError::kNone;
  }

  if (errno != EINPROGRESS && errno != EALREADY)
  {
    return TransportError::kConnectFailure;
  }

  pollfd poll_descriptor{};
  poll_descriptor.fd = fd;
  poll_descriptor.events = POLLOUT;

  for (;;)
  {
    const int timeout_for_poll =
        timeout_ms > static_cast<std::uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(timeout_ms);
    const int poll_result = ::poll(&poll_descriptor, 1, timeout_for_poll);
    if (poll_result > 0)
    {
      int socket_error = 0;
      socklen_t socket_error_size = sizeof(socket_error);
      if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) != 0)
      {
        return TransportError::kUnknown;
      }
      if (socket_error == 0)
      {
        return TransportError::kNone;
      }
      return socket_error == ETIMEDOUT ? TransportError::kTimeout
                                       : TransportError::kConnectFailure;
    }

    if (poll_result == 0)
    {
      return TransportError::kTimeout;
    }

    if (errno == EINTR)
    {
      continue;
    }

    return TransportError::kUnknown;
  }
}

ReadResult MakeClosedReadResult(TransportMetrics& metrics)
{
  NoteReadError(metrics, TransportError::kClosed);
  return ReadResult{0u, TransportStatus::kClosed, TransportError::kClosed};
}

WriteResult MakeClosedWriteResult(TransportMetrics& metrics)
{
  NoteWriteError(metrics, TransportError::kClosed);
  return WriteResult{0u, TransportStatus::kClosed, TransportError::kClosed};
}

ReadResult MakeInvalidReadResult(TransportMetrics& metrics)
{
  NoteReadError(metrics, TransportError::kInvalidArgument);
  return ReadResult{0u, TransportStatus::kError, TransportError::kInvalidArgument};
}

WriteResult MakeInvalidWriteResult(TransportMetrics& metrics)
{
  NoteWriteError(metrics, TransportError::kInvalidArgument);
  return WriteResult{0u, TransportStatus::kError, TransportError::kInvalidArgument};
}

}  // namespace

TcpClientTransport::TcpClientTransport(const TcpClientConfig& config)
{
  Open(config);
}

TcpClientTransport::~TcpClientTransport()
{
  Close();
}

TransportError TcpClientTransport::Open(const TcpClientConfig& config)
{
  Close();

  if (config.host.empty() || config.port == 0u)
  {
    metrics_.last_error = TransportError::kInvalidArgument;
    return TransportError::kInvalidArgument;
  }

  config_ = config;
  use_generic_fd_io_ = false;

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* results = nullptr;
  const std::string service = std::to_string(config.port);
  const int address_lookup_result =
      ::getaddrinfo(config.host.c_str(), service.c_str(), &hints, &results);
  if (address_lookup_result != 0 || results == nullptr)
  {
    metrics_.last_error = TransportError::kConnectFailure;
    return TransportError::kConnectFailure;
  }

  TransportError last_error = TransportError::kConnectFailure;
  for (const addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next)
  {
    const int fd = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (fd < 0)
    {
      last_error = TransportError::kConnectFailure;
      continue;
    }

    const TransportError connect_error =
        ConnectWithTimeout(fd, candidate->ai_addr, candidate->ai_addrlen, config.connect_timeout_ms);
    if (connect_error != TransportError::kNone)
    {
      ::close(fd);
      last_error = connect_error;
      continue;
    }

    const TransportError configure_error = ConfigureConnectedSocket(fd, config);
    if (configure_error != TransportError::kNone)
    {
      ::close(fd);
      last_error = configure_error;
      continue;
    }

    fd_ = fd;
    use_generic_fd_io_ = false;
    metrics_.last_error = TransportError::kNone;
    ::freeaddrinfo(results);
    return TransportError::kNone;
  }

  ::freeaddrinfo(results);
  metrics_.last_error = last_error;
  return last_error;
}

TransportError TcpClientTransport::AdoptConnectedSocket(const int fd, const TcpClientConfig& config)
{
  Close();

  if (fd < 0)
  {
    metrics_.last_error = TransportError::kInvalidArgument;
    return TransportError::kInvalidArgument;
  }

  const TransportError configure_error = ConfigureConnectedSocket(fd, config);
  if (configure_error != TransportError::kNone)
  {
    metrics_.last_error = configure_error;
    ::close(fd);
    return configure_error;
  }

  fd_ = fd;
  use_generic_fd_io_ = false;
  config_ = config;
  metrics_.last_error = TransportError::kNone;
  return TransportError::kNone;
}

ReadResult TcpClientTransport::Read(std::uint8_t* destination, const std::size_t capacity)
{
  if (!IsOpen())
  {
    return MakeClosedReadResult(metrics_);
  }

  if (capacity == 0u)
  {
    return ReadResult{};
  }

  if (destination == nullptr)
  {
    return MakeInvalidReadResult(metrics_);
  }

  if (!config_.nonblocking)
  {
    const WaitForSocketResult wait_result =
        WaitForSocketEvent(fd_, POLLIN, config_.read_timeout_ms);
    if (wait_result == WaitForSocketResult::kTimeout)
    {
      return ReadResult{0u, TransportStatus::kOk, TransportError::kNone};
    }
    if (wait_result == WaitForSocketResult::kError)
    {
      NoteReadError(metrics_, TransportError::kReadFailure);
      return ReadResult{0u, TransportStatus::kError, TransportError::kReadFailure};
    }
  }

  for (;;)
  {
    const ssize_t bytes_read =
        use_generic_fd_io_ ? ::read(fd_, destination, capacity) : ::recv(fd_, destination, capacity, 0);
    if (bytes_read > 0)
    {
      NoteReadBytes(metrics_, static_cast<std::size_t>(bytes_read));
      return ReadResult{static_cast<std::size_t>(bytes_read),
                        TransportStatus::kOk,
                        TransportError::kNone};
    }

    if (bytes_read == 0)
    {
      return ReadResult{0u, TransportStatus::kEndOfStream, TransportError::kNone};
    }

    if (errno == EINTR)
    {
      continue;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      return ReadResult{0u, TransportStatus::kOk, TransportError::kNone};
    }

    NoteReadError(metrics_, TransportError::kReadFailure);
    return ReadResult{0u, TransportStatus::kError, TransportError::kReadFailure};
  }
}

WriteResult TcpClientTransport::Write(const std::uint8_t* data, const std::size_t size)
{
  if (!IsOpen())
  {
    return MakeClosedWriteResult(metrics_);
  }

  if (size == 0u)
  {
    return WriteResult{};
  }

  if (data == nullptr)
  {
    return MakeInvalidWriteResult(metrics_);
  }

  if (!config_.nonblocking)
  {
    const WaitForSocketResult wait_result =
        WaitForSocketEvent(fd_, POLLOUT, config_.write_timeout_ms);
    if (wait_result == WaitForSocketResult::kTimeout)
    {
      return WriteResult{0u, TransportStatus::kOk, TransportError::kNone};
    }
    if (wait_result == WaitForSocketResult::kError)
    {
      NoteWriteError(metrics_, TransportError::kWriteFailure);
      return WriteResult{0u, TransportStatus::kError, TransportError::kWriteFailure};
    }
  }

  for (;;)
  {
    ssize_t bytes_written = -1;
    if (use_generic_fd_io_)
    {
      bytes_written = ::write(fd_, data, size);
    }
    else
    {
#ifdef MSG_NOSIGNAL
      constexpr int kSendFlags = MSG_NOSIGNAL;
#else
      constexpr int kSendFlags = 0;
#endif
      bytes_written = ::send(fd_, data, size, kSendFlags);
    }
    if (bytes_written >= 0)
    {
      NoteWrittenBytes(metrics_, static_cast<std::size_t>(bytes_written));
      return WriteResult{static_cast<std::size_t>(bytes_written),
                         TransportStatus::kOk,
                         TransportError::kNone};
    }

    if (errno == EINTR)
    {
      continue;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      return WriteResult{0u, TransportStatus::kOk, TransportError::kNone};
    }

    NoteWriteError(metrics_, TransportError::kWriteFailure);
    return WriteResult{0u, TransportStatus::kError, TransportError::kWriteFailure};
  }
}

bool TcpClientTransport::IsOpen() const
{
  return fd_ >= 0;
}

void TcpClientTransport::Close()
{
  if (fd_ >= 0)
  {
    ::close(fd_);
    fd_ = -1;
  }
  use_generic_fd_io_ = false;
}

int TcpClientTransport::native_fd() const
{
  return fd_;
}

const TcpClientConfig& TcpClientTransport::config() const
{
  return config_;
}

const TransportMetrics& TcpClientTransport::metrics() const
{
  return metrics_;
}

}  // namespace universal_gnss_transport

#endif
