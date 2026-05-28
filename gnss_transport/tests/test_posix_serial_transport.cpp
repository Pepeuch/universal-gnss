#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(__linux__)

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "universal_gnss_transport/posix_serial_transport.hpp"

namespace
{

using universal_gnss_transport::PosixSerialConfig;
using universal_gnss_transport::PosixSerialTransport;
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

class PseudoTerminal
{
public:
  PseudoTerminal() = default;

  bool Open()
  {
    master_fd_ = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd_ < 0)
    {
      return false;
    }

    if (::grantpt(master_fd_) != 0 || ::unlockpt(master_fd_) != 0)
    {
      Close();
      return false;
    }

    char* slave_name = ::ptsname(master_fd_);
    if (slave_name == nullptr)
    {
      Close();
      return false;
    }

    slave_path_ = slave_name;
    return true;
  }

  ~PseudoTerminal()
  {
    Close();
  }

  PseudoTerminal(const PseudoTerminal&) = delete;
  PseudoTerminal& operator=(const PseudoTerminal&) = delete;

  const std::string& slave_path() const
  {
    return slave_path_;
  }

  bool WriteMaster(const std::vector<std::uint8_t>& data)
  {
    return WriteAll(master_fd_, data.data(), data.size());
  }

  std::vector<std::uint8_t> ReadMasterExact(const std::size_t size)
  {
    std::vector<std::uint8_t> buffer(size, 0u);
    std::size_t offset = 0u;
    while (offset < size)
    {
      const ssize_t bytes_read = ::read(master_fd_,
                                        buffer.data() + static_cast<std::ptrdiff_t>(offset),
                                        size - offset);
      if (bytes_read <= 0)
      {
        buffer.resize(offset);
        break;
      }
      offset += static_cast<std::size_t>(bytes_read);
    }
    return buffer;
  }

private:
  static bool WriteAll(const int fd, const std::uint8_t* data, const std::size_t size)
  {
    std::size_t offset = 0u;
    while (offset < size)
    {
      const ssize_t bytes_written =
          ::write(fd, data + static_cast<std::ptrdiff_t>(offset), size - offset);
      if (bytes_written <= 0)
      {
        return false;
      }
      offset += static_cast<std::size_t>(bytes_written);
    }
    return true;
  }

  void Close()
  {
    if (master_fd_ >= 0)
    {
      ::close(master_fd_);
      master_fd_ = -1;
    }
  }

  int master_fd_{-1};
  std::string slave_path_{};
};

void TestOpenReadWriteClose(TestContext& ctx)
{
  PseudoTerminal pty;
  ctx.Expect(pty.Open(), "pseudo-terminal fixture should open");

  PosixSerialTransport serial;
  const auto open_error = serial.Open(PosixSerialConfig{pty.slave_path(), 115200u, false, 0u});
  ctx.Expect(open_error == TransportError::kNone && serial.IsOpen(),
             "serial transport should open a pseudo-terminal slave");

  const std::vector<std::uint8_t> inbound = {0x10u, 0x20u, 0x30u};
  ctx.Expect(pty.WriteMaster(inbound), "pseudo-terminal master should accept inbound data");

  std::vector<std::uint8_t> read_buffer(3u, 0u);
  const auto read_result = serial.Read(read_buffer.data(), read_buffer.size());
  ctx.Expect(read_result.status == TransportStatus::kOk &&
                 read_result.bytes_read == inbound.size() &&
                 read_buffer == inbound,
             "serial transport should read bytes written to the pseudo-terminal master");

  const std::vector<std::uint8_t> outbound = {0xAAu, 0xBBu};
  const auto write_result = serial.Write(outbound.data(), outbound.size());
  ctx.Expect(write_result.status == TransportStatus::kOk &&
                 write_result.bytes_written == outbound.size() &&
                 pty.ReadMasterExact(outbound.size()) == outbound,
             "serial transport should write bytes to the pseudo-terminal master");

  ctx.Expect(serial.metrics().bytes_read == inbound.size() &&
                 serial.metrics().bytes_written == outbound.size(),
             "serial transport should update byte counters on successful I/O");

  serial.Close();
  ctx.Expect(!serial.IsOpen(), "serial transport close should release the file descriptor");
}

void TestNonblockingReadWithoutData(TestContext& ctx)
{
  PseudoTerminal pty;
  ctx.Expect(pty.Open(), "pseudo-terminal fixture should open for nonblocking test");

  PosixSerialTransport serial;
  const auto open_error = serial.Open(PosixSerialConfig{pty.slave_path(), 115200u, true, 0u});
  ctx.Expect(open_error == TransportError::kNone,
             "serial transport should open in nonblocking mode");

  std::vector<std::uint8_t> buffer(4u, 0u);
  const auto read_result = serial.Read(buffer.data(), buffer.size());
  ctx.Expect(read_result.status == TransportStatus::kOk &&
                 read_result.bytes_read == 0u &&
                 serial.metrics().bytes_read == 0u,
             "nonblocking read without data should return zero bytes without error");
}

void TestInvalidConfigurationRejected(TestContext& ctx)
{
  PosixSerialTransport serial;

  ctx.Expect(serial.Open(PosixSerialConfig{}) == TransportError::kInvalidArgument,
             "empty serial path should be rejected");
  ctx.Expect(serial.Open(PosixSerialConfig{"/dev/null", 12345u, false, 0u}) ==
                 TransportError::kUnsupported,
             "unsupported baud rate should be rejected");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestOpenReadWriteClose(ctx);
  TestNonblockingReadWithoutData(ctx);
  TestInvalidConfigurationRejected(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_transport POSIX serial tests passed\n";
  return EXIT_SUCCESS;
}

#else

int main()
{
  std::cout << "POSIX serial transport tests skipped on non-Linux platforms\n";
  return EXIT_SUCCESS;
}

#endif
