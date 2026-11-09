#include "universal_gnss_transport/posix_serial_transport.hpp"

#if defined(__linux__)

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include <fcntl.h>
#include <poll.h>
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
      ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY));
  options.c_oflag &= static_cast<tcflag_t>(~OPOST);
  options.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL | ICANON | ISIG | IEXTEN));
  options.c_cflag &= static_cast<tcflag_t>(~(CSIZE | PARENB | CSTOPB));
#ifdef CRTSCTS
  options.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
#endif
  options.c_cflag |= CS8;
  options.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
}

constexpr std::uint64_t kMillisecondsPerDecisecond = 100u;
constexpr std::uint64_t kMaximumVtimeTimeoutMs =
    static_cast<std::uint64_t>(std::numeric_limits<cc_t>::max()) * kMillisecondsPerDecisecond;

bool HasRepresentableReadTimeout(const PosixSerialConfig& config)
{
  return config.nonblocking ||
         static_cast<std::uint64_t>(config.read_timeout_ms) <= kMaximumVtimeTimeoutMs;
}

TransportError ConfigureSerialPort(const int fd, const PosixSerialConfig& config)
{
  if (!HasRepresentableReadTimeout(config))
  {
    return TransportError::kInvalidArgument;
  }

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
    const auto deciseconds = ((config.read_timeout_ms - 1u) / kMillisecondsPerDecisecond) + 1u;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = static_cast<cc_t>(deciseconds);
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

enum class PollResult
{
  kReady,
  kIdle,
  kCancelled,
  kFailure,
};

PollResult WaitForEvent(const int fd,
                        const int wakeup_fd,
                        const short events,
                        const int timeout_ms,
                        bool* hangup = nullptr)
{
  std::array<pollfd, 2> descriptors = {
      pollfd{fd, events, 0},
      pollfd{wakeup_fd, POLLIN, 0},
  };

  for (;;)
  {
    const int result = ::poll(descriptors.data(), descriptors.size(), timeout_ms);
    if (result == 0)
    {
      return PollResult::kIdle;
    }
    if (result < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      return PollResult::kFailure;
    }
    if ((descriptors[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0)
    {
      return PollResult::kCancelled;
    }
    if ((descriptors[0].revents & (POLLERR | POLLNVAL)) != 0)
    {
      return PollResult::kFailure;
    }
    if ((descriptors[0].revents & (events | POLLHUP)) != 0)
    {
      if (hangup != nullptr)
      {
        *hangup = (descriptors[0].revents & POLLHUP) != 0;
      }
      return PollResult::kReady;
    }
  }
}

int ReadPollTimeoutMs(const PosixSerialConfig& config)
{
  if (config.nonblocking)
  {
    return 0;
  }
  if (config.read_timeout_ms == 0u)
  {
    return -1;
  }
  return static_cast<int>(config.read_timeout_ms);
}

void SignalWakeup(const int wakeup_write_fd)
{
  if (wakeup_write_fd < 0)
  {
    return;
  }

  const std::uint8_t signal = 1u;
  while (::write(wakeup_write_fd, &signal, sizeof(signal)) < 0 && errno == EINTR)
  {
  }
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
  std::lock_guard<std::mutex> lifecycle_lock(open_close_mutex_);
  CloseImpl();

  std::unique_lock<std::mutex> lock(mutex_);
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
  if (!HasRepresentableReadTimeout(config))
  {
    metrics_.last_error = TransportError::kInvalidArgument;
    return TransportError::kInvalidArgument;
  }

  int wakeup_fds[2] = {-1, -1};
  if (::pipe2(wakeup_fds, O_CLOEXEC | O_NONBLOCK) != 0)
  {
    metrics_.last_error = TransportError::kUnknown;
    return TransportError::kUnknown;
  }

  // I/O uses poll followed by a nonblocking syscall. Close() wakes poll and
  // retires all active operations before it closes the descriptor.
  const int fd = ::open(config.device_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0)
  {
    ::close(wakeup_fds[0]);
    ::close(wakeup_fds[1]);
    metrics_.last_error = TransportError::kUnknown;
    return TransportError::kUnknown;
  }

  const auto configure_error = ConfigureSerialPort(fd, config);
  if (configure_error != TransportError::kNone)
  {
    ::close(fd);
    ::close(wakeup_fds[0]);
    ::close(wakeup_fds[1]);
    metrics_.last_error = configure_error;
    return configure_error;
  }

  fd_ = fd;
  wakeup_read_fd_ = wakeup_fds[0];
  wakeup_write_fd_ = wakeup_fds[1];
  config_ = config;
  metrics_.last_error = TransportError::kNone;
  return TransportError::kNone;
}

ReadResult PosixSerialTransport::Read(std::uint8_t* destination, const std::size_t capacity)
{
  int fd = -1;
  int wakeup_fd = -1;
  if (!AcquireOperation(fd, wakeup_fd))
  {
    return ClosedReadResult();
  }

  if (capacity == 0u)
  {
    ReleaseOperation();
    return ReadResult{};
  }
  if (destination == nullptr)
  {
    ReleaseOperation();
    return InvalidReadResult();
  }

  const PosixSerialConfig config = this->config();
  for (;;)
  {
    bool hangup = false;
    const PollResult poll = WaitForEvent(fd, wakeup_fd, POLLIN, ReadPollTimeoutMs(config), &hangup);
    if (poll == PollResult::kIdle)
    {
      ReleaseOperation();
      return ReadResult{0u, TransportStatus::kOk, TransportError::kNone};
    }
    if (poll == PollResult::kCancelled)
    {
      ReleaseOperation();
      return ClosedReadResult();
    }
    if (poll == PollResult::kFailure)
    {
      ReleaseOperation();
      NoteReadError(TransportError::kReadFailure);
      return ReadResult{0u, TransportStatus::kError, TransportError::kReadFailure};
    }

    const ssize_t bytes_read = ::read(fd, destination, capacity);
    if (bytes_read > 0)
    {
      ReleaseOperation();
      NoteReadBytes(static_cast<std::size_t>(bytes_read));
      return ReadResult{
          static_cast<std::size_t>(bytes_read), TransportStatus::kOk, TransportError::kNone};
    }
    if (bytes_read == 0)
    {
      ReleaseOperation();
      if (hangup || (!config.nonblocking && config.read_timeout_ms == 0u))
      {
        return ReadResult{0u, TransportStatus::kEndOfStream, TransportError::kNone};
      }
      return ReadResult{0u, TransportStatus::kOk, TransportError::kNone};
    }
    if (errno == EINTR)
    {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      ReleaseOperation();
      return ReadResult{0u, TransportStatus::kOk, TransportError::kNone};
    }

    ReleaseOperation();
    NoteReadError(TransportError::kReadFailure);
    return ReadResult{0u, TransportStatus::kError, TransportError::kReadFailure};
  }
}

WriteResult PosixSerialTransport::Write(const std::uint8_t* data, const std::size_t size)
{
  int fd = -1;
  int wakeup_fd = -1;
  if (!AcquireOperation(fd, wakeup_fd))
  {
    return ClosedWriteResult();
  }

  if (size == 0u)
  {
    ReleaseOperation();
    return WriteResult{};
  }
  if (data == nullptr)
  {
    ReleaseOperation();
    return InvalidWriteResult();
  }

  const PosixSerialConfig config = this->config();
  for (;;)
  {
    const PollResult poll = WaitForEvent(fd, wakeup_fd, POLLOUT, config.nonblocking ? 0 : -1);
    if (poll == PollResult::kIdle)
    {
      ReleaseOperation();
      return WriteResult{0u, TransportStatus::kOk, TransportError::kNone};
    }
    if (poll == PollResult::kCancelled)
    {
      ReleaseOperation();
      return ClosedWriteResult();
    }
    if (poll == PollResult::kFailure)
    {
      ReleaseOperation();
      NoteWriteError(TransportError::kWriteFailure);
      return WriteResult{0u, TransportStatus::kError, TransportError::kWriteFailure};
    }

    const ssize_t bytes_written = ::write(fd, data, size);
    if (bytes_written >= 0)
    {
      ReleaseOperation();
      NoteWrittenBytes(static_cast<std::size_t>(bytes_written));
      return WriteResult{
          static_cast<std::size_t>(bytes_written), TransportStatus::kOk, TransportError::kNone};
    }
    if (errno == EINTR)
    {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      ReleaseOperation();
      return WriteResult{0u, TransportStatus::kOk, TransportError::kNone};
    }

    ReleaseOperation();
    NoteWriteError(TransportError::kWriteFailure);
    return WriteResult{0u, TransportStatus::kError, TransportError::kWriteFailure};
  }
}

bool PosixSerialTransport::IsOpen() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return fd_ >= 0 && !closing_;
}

void PosixSerialTransport::Close()
{
  std::lock_guard<std::mutex> lifecycle_lock(open_close_mutex_);
  CloseImpl();
}

void PosixSerialTransport::CloseImpl()
{
  int fd = -1;
  int wakeup_read_fd = -1;
  int wakeup_write_fd = -1;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    lifecycle_condition_.wait(lock, [this] { return !closing_; });
    if (fd_ < 0)
    {
      return;
    }

    closing_ = true;
    SignalWakeup(wakeup_write_fd_);
    lifecycle_condition_.wait(lock, [this] { return active_operations_ == 0u; });
    fd = fd_;
    wakeup_read_fd = wakeup_read_fd_;
    wakeup_write_fd = wakeup_write_fd_;
    fd_ = -1;
    wakeup_read_fd_ = -1;
    wakeup_write_fd_ = -1;
    closing_ = false;
  }
  lifecycle_condition_.notify_all();

  ::close(fd);
  ::close(wakeup_read_fd);
  ::close(wakeup_write_fd);
}

int PosixSerialTransport::native_fd() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return fd_;
}

PosixSerialConfig PosixSerialTransport::config() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

TransportMetrics PosixSerialTransport::metrics() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return metrics_;
}

bool PosixSerialTransport::AcquireOperation(int& fd, int& wakeup_fd)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (fd_ < 0 || closing_)
  {
    return false;
  }
  ++active_operations_;
  fd = fd_;
  wakeup_fd = wakeup_read_fd_;
  return true;
}

void PosixSerialTransport::ReleaseOperation()
{
  std::lock_guard<std::mutex> lock(mutex_);
  --active_operations_;
  if (active_operations_ == 0u)
  {
    lifecycle_condition_.notify_all();
  }
}

ReadResult PosixSerialTransport::ClosedReadResult()
{
  return ReadResult{0u, TransportStatus::kClosed, TransportError::kClosed};
}

WriteResult PosixSerialTransport::ClosedWriteResult()
{
  return WriteResult{0u, TransportStatus::kClosed, TransportError::kClosed};
}

ReadResult PosixSerialTransport::InvalidReadResult()
{
  NoteReadError(TransportError::kInvalidArgument);
  return ReadResult{0u, TransportStatus::kError, TransportError::kInvalidArgument};
}

WriteResult PosixSerialTransport::InvalidWriteResult()
{
  NoteWriteError(TransportError::kInvalidArgument);
  return WriteResult{0u, TransportStatus::kError, TransportError::kInvalidArgument};
}

void PosixSerialTransport::NoteReadBytes(const std::size_t bytes_read)
{
  std::lock_guard<std::mutex> lock(mutex_);
  universal_gnss_transport::NoteReadBytes(metrics_, bytes_read);
}

void PosixSerialTransport::NoteWrittenBytes(const std::size_t bytes_written)
{
  std::lock_guard<std::mutex> lock(mutex_);
  universal_gnss_transport::NoteWrittenBytes(metrics_, bytes_written);
}

void PosixSerialTransport::NoteReadError(const TransportError error)
{
  std::lock_guard<std::mutex> lock(mutex_);
  universal_gnss_transport::NoteReadError(metrics_, error);
}

void PosixSerialTransport::NoteWriteError(const TransportError error)
{
  std::lock_guard<std::mutex> lock(mutex_);
  universal_gnss_transport::NoteWriteError(metrics_, error);
}

}  // namespace universal_gnss_transport

#endif
