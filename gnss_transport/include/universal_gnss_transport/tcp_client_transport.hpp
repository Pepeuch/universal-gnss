#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/transport_metrics.hpp"

struct ssl_ctx_st;
struct ssl_st;

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
  bool tls_enabled{false};
  // Keep this enabled outside deterministic local test environments.
  bool tls_verify_peer{true};
  // Empty selects host. A distinct value is useful only for a documented TLS
  // endpoint name; custom trust roots remain outside this transport contract.
  std::string tls_server_name{};
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
  ::ssl_ctx_st* tls_context_{nullptr};
  ::ssl_st* tls_session_{nullptr};
  TcpClientConfig config_{};
  TransportMetrics metrics_{};
};

#endif

}  // namespace universal_gnss_transport
