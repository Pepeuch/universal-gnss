#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/transport_metrics.hpp"

namespace universal_gnss_transport
{

#if defined(__linux__)

struct UdpClientConfig
{
  std::string host{};
  std::uint16_t port{0u};
  std::uint32_t read_timeout_ms{0u};
  bool nonblocking{false};
};

class UdpClientTransport final : public ByteDuplex
{
public:
  ~UdpClientTransport() override;

  TransportError Open(const UdpClientConfig& config);
  ReadResult Read(std::uint8_t* destination, std::size_t capacity) override;
  WriteResult Write(const std::uint8_t* data, std::size_t size) override;
  bool IsOpen() const override;
  void Close() override;

  const TransportMetrics& metrics() const;

private:
  int fd_{-1};
  UdpClientConfig config_{};
  TransportMetrics metrics_{};
};

#endif

}  // namespace universal_gnss_transport
