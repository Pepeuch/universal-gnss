#include "universal_gnss_transport/posix_serial_transport.hpp"

#if defined(__linux__)

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace universal_gnss_transport
{

namespace
{

std::optional<speed_t> MapBaudRate(const std::uint32_t baud_rate)
{
  switch (baud_rate)
  {
    case 4800u:
      return B4800;
    case 9600u:
      return B9600;
    case 19200u:
      return B19200;
    case 38400u:
      return B38400;
    case 57600u:
      return B57600;
    case 115200u:
      return B115200;
#ifdef B230400
    case 230400u:
      return B230400;
#endif
#ifdef B460800
    case 460800u:
      return B460800;
#endif
#ifdef B921600
    case 921600u:
      return B921600;
#endif
    default:
      return std::nullopt;
  }
}

void ConfigureRawMode(termios& options)
{
  options.c_iflag &= static_cast<tcflag_t>(
      ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON));
  options.c_oflag &= static_cast<tcflag_t>(~OPOST);
  options.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL | ICANON | ISIG | IEXTEN));
  options.c_cflag &= static_cast<tcflag_t>(~(CSIZE | PARENB));
  options.c_cflag |= CS8;
  options.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
}

TransportError ConfigureSerialPort(const int fd, const PosixSerialConfig& config)
{
  termios options{};
  if (::tcgetattr(fd, &options) != 0)
  {
    return TransportError::kUnknown;
  }

  ConfigureRawMode(options);

  const auto baud = MapBaudRate(config.baud_rate);
  if (!baud.has_value())
  {
    return TransportError::kUnsupported;
  }

  if (::cfsetispeed(&options, *baud) != 0 || ::cfsetospeed(&options, *baud) != 0)
  {
    return TransportError::kUnsupported;
  }

  if (config.nonblocking)
  {
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;
  }
  else if (config.read_timeout_ms > 0u)
  {
    const auto deciseconds = static_cast<cc_t>((config.read_timeout_ms + 99u) / 100u);
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = deciseconds > 0u ? deciseconds : 1u;
  }
  else
  {
    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 0;
  }

  if (::tcsetattr(fd, TCSANOW, &options) != 0)
  {
    return TransportError::kUnknown;
  }

  return TransportError::kNone;
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

PosixSerialTransport::PosixSerialTransport(const PosixSerialConfig& config)
{
  Open(config);
}

PosixSerialTransport::~PosixSerialTransport()
{
  Close();
}

TransportError PosixSerialTransport::Open(const PosixSerialConfig& config)
{
  Close();

  if (config.device_path.empty())
  {
    metrics_.last_error = TransportError::kInvalidArgument;
    return TransportError::kInvalidArgument;
  }

  if (!MapBaudRate(config.baud_rate).has_value())
  {
    metrics_.last_error = TransportError::kUnsupported;
    return TransportError::kUnsupported;
  }

  config_ = config;

  int open_flags = O_RDWR | O_NOCTTY;
  if (config.nonblocking)
  {
    open_flags |= O_NONBLOCK;
  }

  const int fd = ::open(config.device_path.c_str(), open_flags);
  if (fd < 0)
  {
    metrics_.last_error = TransportError::kUnknown;
    return TransportError::kUnknown;
  }

  const auto configure_error = ConfigureSerialPort(fd, config);
  if (configure_error != TransportError::kNone)
  {
    ::close(fd);
    metrics_.last_error = configure_error;
    return configure_error;
  }

  const int current_flags = ::fcntl(fd, F_GETFD);
  if (current_flags >= 0)
  {
    ::fcntl(fd, F_SETFD, current_flags | FD_CLOEXEC);
  }

  fd_ = fd;
  metrics_.last_error = TransportError::kNone;
  return TransportError::kNone;
}

ReadResult PosixSerialTransport::Read(std::uint8_t* destination, const std::size_t capacity)
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

  for (;;)
  {
    const ssize_t bytes_read = ::read(fd_, destination, capacity);
    if (bytes_read > 0)
    {
      NoteReadBytes(metrics_, static_cast<std::size_t>(bytes_read));
      return ReadResult{static_cast<std::size_t>(bytes_read),
                        TransportStatus::kOk,
                        TransportError::kNone};
    }

    if (bytes_read == 0)
    {
      if (config_.nonblocking || config_.read_timeout_ms > 0u)
      {
        return ReadResult{0u, TransportStatus::kOk, TransportError::kNone};
      }
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

WriteResult PosixSerialTransport::Write(const std::uint8_t* data, const std::size_t size)
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

  for (;;)
  {
    const ssize_t bytes_written = ::write(fd_, data, size);
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

bool PosixSerialTransport::IsOpen() const
{
  return fd_ >= 0;
}

void PosixSerialTransport::Close()
{
  if (fd_ >= 0)
  {
    ::close(fd_);
    fd_ = -1;
  }
}

int PosixSerialTransport::native_fd() const
{
  return fd_;
}

const PosixSerialConfig& PosixSerialTransport::config() const
{
  return config_;
}

const TransportMetrics& PosixSerialTransport::metrics() const
{
  return metrics_;
}

}  // namespace universal_gnss_transport

#endif
