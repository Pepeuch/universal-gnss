#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/transport_metrics.hpp"

namespace universal_gnss_transport
{

#if defined(__linux__)

struct TcpClientConfig
{
  std::string host{};
  std::uint16_t port{0u};
  std::uint32_t connect_timeout_ms{0u};
  std::uint32_t read_timeout_ms{0u};
  std::uint32_t write_timeout_ms{0u};
  bool nonblocking{false};
  bool tcp_nodelay{false};
};

class TcpClientTransport : public ByteDuplex
{
public:
  TcpClientTransport() = default;
  explicit TcpClientTransport(const TcpClientConfig& config);
  ~TcpClientTransport() override;

  TcpClientTransport(const TcpClientTransport&) = delete;
  TcpClientTransport& operator=(const TcpClientTransport&) = delete;
  TcpClientTransport(TcpClientTransport&&) = delete;
  TcpClientTransport& operator=(TcpClientTransport&&) = delete;

  TransportError Open(const TcpClientConfig& config);
  TransportError AdoptConnectedSocket(int fd, const TcpClientConfig& config = {});

  ReadResult Read(std::uint8_t* destination, std::size_t capacity) override;
  WriteResult Write(const std::uint8_t* data, std::size_t size) override;
  bool IsOpen() const override;
  void Close() override;

  int native_fd() const;

  const TcpClientConfig& config() const;
  const TransportMetrics& metrics() const;

private:
  int fd_{-1};
  bool use_generic_fd_io_{false};
  TcpClientConfig config_{};
  TransportMetrics metrics_{};
};

#endif

}  // namespace universal_gnss_transport
