#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ssl.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace universal_gnss_transport::test
{

#ifndef UNIVERSAL_GNSS_TLS_FIXTURE_DIR
#error "UNIVERSAL_GNSS_TLS_FIXTURE_DIR must name the versioned TLS test fixtures"
#endif

class TlsLoopbackServer
{
public:
  struct Options
  {
    bool require_client_certificate{false};
    std::function<bool(SSL*)> session{};
  };

  TlsLoopbackServer() = default;
  explicit TlsLoopbackServer(Options options) : options_(std::move(options)) {}

  ~TlsLoopbackServer()
  {
    Join();
    if (listener_fd_ >= 0)
    {
      ::close(listener_fd_);
    }
  }

  TlsLoopbackServer(const TlsLoopbackServer&) = delete;
  TlsLoopbackServer& operator=(const TlsLoopbackServer&) = delete;

  bool Start()
  {
    listener_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd_ < 0)
    {
      return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener_fd_, 1) != 0)
    {
      return false;
    }
    socklen_t address_size = sizeof(address);
    if (::getsockname(listener_fd_, reinterpret_cast<sockaddr*>(&address), &address_size) != 0)
    {
      return false;
    }
    port_ = ntohs(address.sin_port);
    server_thread_ = std::thread(&TlsLoopbackServer::Run, this);
    return true;
  }

  std::uint16_t port() const { return port_; }

  bool Join()
  {
    if (server_thread_.joinable())
    {
      server_thread_.join();
    }
    return session_succeeded_;
  }

  static bool ReadExact(SSL* session, std::uint8_t* destination, const std::size_t size)
  {
    std::size_t offset = 0u;
    while (offset < size)
    {
      const int read_size = SSL_read(session, destination + offset, static_cast<int>(size - offset));
      if (read_size <= 0)
      {
        return false;
      }
      offset += static_cast<std::size_t>(read_size);
    }
    return true;
  }

  static bool WriteAll(SSL* session, const std::uint8_t* data, const std::size_t size)
  {
    std::size_t offset = 0u;
    while (offset < size)
    {
      const int written = SSL_write(session, data + offset, static_cast<int>(size - offset));
      if (written <= 0)
      {
        return false;
      }
      offset += static_cast<std::size_t>(written);
    }
    return true;
  }

private:
  void Run()
  {
    pollfd listener{};
    listener.fd = listener_fd_;
    listener.events = POLLIN;
    if (::poll(&listener, 1, 5000) <= 0)
    {
      return;
    }
    const int connection_fd = ::accept(listener_fd_, nullptr, nullptr);
    if (connection_fd < 0)
    {
      return;
    }

    SSL_CTX* context = SSL_CTX_new(TLS_server_method());
    SSL* session = nullptr;
    const std::string fixture_dir = UNIVERSAL_GNSS_TLS_FIXTURE_DIR;
    const bool configured = context != nullptr && SSL_CTX_set_max_proto_version(context, TLS1_2_VERSION) == 1 &&
                            SSL_CTX_use_certificate_chain_file(
                                context, (fixture_dir + "/server.crt").c_str()) == 1 &&
                            SSL_CTX_use_PrivateKey_file(context, (fixture_dir + "/server.key").c_str(),
                                                        SSL_FILETYPE_PEM) == 1 &&
                            SSL_CTX_check_private_key(context) == 1;
    if (configured && options_.require_client_certificate)
    {
      session_succeeded_ = SSL_CTX_load_verify_locations(context, (fixture_dir + "/ca.crt").c_str(),
                                                          nullptr) == 1;
      if (session_succeeded_)
      {
        SSL_CTX_set_verify(context, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
      }
    }
    else
    {
      session_succeeded_ = configured;
    }
    if (session_succeeded_)
    {
      session = SSL_new(context);
      session_succeeded_ = session != nullptr && SSL_set_fd(session, connection_fd) == 1 &&
                           SSL_accept(session) == 1;
      if (session_succeeded_ && options_.session)
      {
        session_succeeded_ = options_.session(session);
      }
    }
    if (session != nullptr)
    {
      SSL_free(session);
    }
    SSL_CTX_free(context);
    ::close(connection_fd);
  }

  Options options_{};
  int listener_fd_{-1};
  std::uint16_t port_{0u};
  bool session_succeeded_{false};
  std::thread server_thread_{};
};

}  // namespace universal_gnss_transport::test
