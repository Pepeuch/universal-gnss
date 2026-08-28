#pragma once

#include <cstddef>
#include <cstdint>
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

  int native_fd() const;

  const PosixSerialConfig& config() const;
  const TransportMetrics& metrics() const;

private:
  int fd_{-1};
  PosixSerialConfig config_{};
  TransportMetrics metrics_{};
};

#endif

}  // namespace universal_gnss_transport
