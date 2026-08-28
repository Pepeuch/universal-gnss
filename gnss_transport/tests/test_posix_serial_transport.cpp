#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#if defined(__linux__)

#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
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

void TestOpenClearsInheritedRawModeFlags(TestContext& ctx)
{
  PseudoTerminal pty;
  ctx.Expect(pty.Open(), "pseudo-terminal fixture should open for raw-mode inheritance test");

  const int inherited_fd = ::open(pty.slave_path().c_str(), O_RDWR | O_NOCTTY);
  ctx.Expect(inherited_fd >= 0, "pseudo-terminal slave should open for termios setup");
  if (inherited_fd < 0)
  {
    return;
  }

  termios inherited{};
  const bool got_attributes = ::tcgetattr(inherited_fd, &inherited) == 0;
  ctx.Expect(got_attributes, "pseudo-terminal slave should provide termios attributes");
  if (!got_attributes)
  {
    ::close(inherited_fd);
    return;
  }

  inherited.c_cflag |= CSTOPB;
#ifdef CRTSCTS
  inherited.c_cflag |= CRTSCTS;
#endif
  inherited.c_iflag |= static_cast<tcflag_t>(IXON | IXOFF | IXANY);
  const bool seeded_attributes = ::tcsetattr(inherited_fd, TCSANOW, &inherited) == 0;
  ctx.Expect(seeded_attributes, "pseudo-terminal slave should retain seeded incompatible flags");
  if (!seeded_attributes)
  {
    ::close(inherited_fd);
    return;
  }

  PosixSerialTransport serial;
  const auto open_error = serial.Open(PosixSerialConfig{pty.slave_path(), 115200u, false, 0u});
  ctx.Expect(open_error == TransportError::kNone,
             "serial transport should open a pseudo-terminal with inherited incompatible flags");

  termios configured{};
  const bool got_configured_attributes = ::tcgetattr(inherited_fd, &configured) == 0;
  ctx.Expect(got_configured_attributes,
             "serial transport raw-mode configuration should be observable on the pseudo-terminal");
  if (got_configured_attributes)
  {
    const bool software_flow_control_disabled =
        (configured.c_iflag & static_cast<tcflag_t>(IXON | IXOFF | IXANY)) == 0;
    const bool framing_is_8n1 = (configured.c_cflag & CSIZE) == CS8 &&
                                (configured.c_cflag & PARENB) == 0 &&
                                (configured.c_cflag & CSTOPB) == 0;
#ifdef CRTSCTS
    const bool hardware_flow_control_disabled = (configured.c_cflag & CRTSCTS) == 0;
#else
    const bool hardware_flow_control_disabled = true;
#endif
    ctx.Expect(software_flow_control_disabled && framing_is_8n1 &&
                   hardware_flow_control_disabled,
               "serial raw mode should clear inherited stop-bit and flow-control flags");
  }

  ::close(inherited_fd);
}

void TestReadTimeoutConversionRejectsUnrepresentableValues(TestContext& ctx)
{
  struct TimeoutCase
  {
    std::uint32_t timeout_ms;
    bool expect_open;
    cc_t expected_vmin;
    cc_t expected_vtime;
  };

  const std::vector<TimeoutCase> cases = {
      {0u, true, 1u, 0u},
      {1u, true, 0u, 1u},
      {99u, true, 0u, 1u},
      {100u, true, 0u, 1u},
      {25500u, true, 0u, 255u},
      {25600u, false, 0u, 0u},
      {std::numeric_limits<std::uint32_t>::max(), false, 0u, 0u},
  };

  for (const auto& test_case : cases)
  {
    PseudoTerminal pty;
    ctx.Expect(pty.Open(), "pseudo-terminal fixture should open for timeout conversion test");
    if (pty.slave_path().empty())
    {
      continue;
    }

    PosixSerialTransport serial;
    const auto open_error = serial.Open(
        PosixSerialConfig{pty.slave_path(), 115200u, false, test_case.timeout_ms});
    if (!test_case.expect_open)
    {
      ctx.Expect(open_error == TransportError::kInvalidArgument && !serial.IsOpen() &&
                     serial.metrics().last_error == TransportError::kInvalidArgument,
                 "unrepresentable serial timeout must be rejected instead of narrowing: " +
                     std::to_string(test_case.timeout_ms));
      continue;
    }

    ctx.Expect(open_error == TransportError::kNone && serial.IsOpen(),
               "representable serial timeout should open: " + std::to_string(test_case.timeout_ms));
    if (open_error != TransportError::kNone)
    {
      continue;
    }

    termios options{};
    const bool got_attributes = ::tcgetattr(serial.native_fd(), &options) == 0;
    ctx.Expect(got_attributes,
               "opened serial timeout case should expose termios attributes: " +
                   std::to_string(test_case.timeout_ms));
    if (got_attributes)
    {
      ctx.Expect(options.c_cc[VMIN] == test_case.expected_vmin &&
                     options.c_cc[VTIME] == test_case.expected_vtime,
                 "serial timeout should convert without overflow or wrap: " +
                     std::to_string(test_case.timeout_ms));
    }
  }
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
  TestOpenClearsInheritedRawModeFlags(ctx);
  TestReadTimeoutConversionRejectsUnrepresentableValues(ctx);
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
