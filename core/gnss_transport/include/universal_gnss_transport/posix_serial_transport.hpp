#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/transport_metrics.hpp"

namespace universal_gnss_transport
{

#if defined(__linux__)

struct PosixSerialConfig
{
  std::string device_path{};
  std::uint32_t baud_rate{115200u};
  bool nonblocking{false};
  // In blocking mode, values beyond the platform VTIME range are rejected by Open().
  std::uint32_t read_timeout_ms{0u};
};

class PosixSerialTransport : public ByteDuplex
{
public:
  PosixSerialTransport() = default;
  explicit PosixSerialTransport(const PosixSerialConfig& config);
  ~PosixSerialTransport() override;

  PosixSerialTransport(const PosixSerialTransport&) = delete;
  PosixSerialTransport& operator=(const PosixSerialTransport&) = delete;
  PosixSerialTransport(PosixSerialTransport&&) = delete;
  PosixSerialTransport& operator=(PosixSerialTransport&&) = delete;

  TransportError Open(const PosixSerialConfig& config);

  ReadResult Read(std::uint8_t* destination, std::size_t capacity) override;
  WriteResult Write(const std::uint8_t* data, std::size_t size) override;
  bool IsOpen() const override;
  void Close() override;

  // This is a diagnostic/testing snapshot only. Callers must not retain or
  // operate on the returned descriptor across another transport operation.
  int native_fd() const;

  PosixSerialConfig config() const;
  TransportMetrics metrics() const;

private:
  void CloseImpl();
  bool AcquireOperation(int& fd, int& wakeup_fd);
  void ReleaseOperation();
  ReadResult ClosedReadResult();
  WriteResult ClosedWriteResult();
  ReadResult InvalidReadResult();
  WriteResult InvalidWriteResult();
  void NoteReadBytes(std::size_t bytes_read);
  void NoteWrittenBytes(std::size_t bytes_written);
  void NoteReadError(TransportError error);
  void NoteWriteError(TransportError error);

  // Serializes Open/Close as a whole; mutex_ protects descriptor state and
  // active read/write leases while CloseImpl waits without holding mutex_.
  std::mutex open_close_mutex_;
  mutable std::mutex mutex_;
  std::condition_variable lifecycle_condition_;
  int fd_{-1};
  int wakeup_read_fd_{-1};
  int wakeup_write_fd_{-1};
  bool closing_{false};
  std::size_t active_operations_{0u};
  PosixSerialConfig config_{};
  TransportMetrics metrics_{};
};

#endif

}  // namespace universal_gnss_transport
