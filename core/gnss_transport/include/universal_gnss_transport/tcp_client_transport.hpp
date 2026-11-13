#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/transport_metrics.hpp"

struct ssl_ctx_st;
struct ssl_st;
#if defined(ESP_PLATFORM)
struct esp_tls;
#endif

namespace universal_gnss_transport
{

#if defined(__linux__) || defined(ESP_PLATFORM)

struct TcpClientConfig
{
  std::string host{};
  std::uint16_t port{0u};
  // TLS: one socket-connect plus handshake deadline; zero selects 5000 ms.
  // Plain TCP: per-address connect timeout; zero retains blocking connect.
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
  // Optional PEM trust bundle loaded in addition to the Linux system trust store.
  // IDF: an explicit file selects a private trust bundle; an empty file setting
  // uses the SDK certificate bundle. IDF never allows tls_verify_peer=false.
  std::string tls_ca_file{};
  std::string tls_client_certificate_file{};
  std::string tls_client_private_key_file{};
  // Optional synchronous TLS-setup cancellation check. The callback
  // must remain valid until Open/AdoptConnectedSocket returns and may be
  // called repeatedly from that same thread. It must be thread-safe if
  // another thread changes the state it observes, and return promptly.
  std::function<bool()> connect_cancelled{};
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
#if defined(ESP_PLATFORM)
  ::esp_tls* idf_tls_{nullptr};
  std::string ca_pem_{}, client_pem_{}, key_pem_{};
#else
  ::ssl_ctx_st* tls_context_{nullptr};
  ::ssl_st* tls_session_{nullptr};
#endif
  TcpClientConfig config_{};
  TransportMetrics metrics_{};
};

#endif

}  // namespace universal_gnss_transport
