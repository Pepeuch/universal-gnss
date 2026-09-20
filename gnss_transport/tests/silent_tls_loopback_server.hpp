#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <thread>

namespace universal_gnss_transport::test
{

// Accepts a real TCP connection and reads the client's TLS hello, but never
// sends a TLS byte or closes first. The client must end the stalled session.
class SilentTlsLoopbackServer
{
public:
  ~SilentTlsLoopbackServer()
  {
    Join();
    if (listener_fd_ >= 0)
    {
      ::close(listener_fd_);
    }
  }

  SilentTlsLoopbackServer(const SilentTlsLoopbackServer&) = delete;
  SilentTlsLoopbackServer& operator=(const SilentTlsLoopbackServer&) = delete;

  SilentTlsLoopbackServer() = default;

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
    worker_ = std::thread(&SilentTlsLoopbackServer::Run, this);
    return true;
  }

  std::uint16_t port() const
  {
    return port_;
  }

  bool accepted() const
  {
    return accepted_.load();
  }

  bool received_client_hello() const
  {
    return received_client_hello_.load();
  }

  bool peer_closed() const
  {
    return peer_closed_.load();
  }

  void Join()
  {
    stop_requested_ = true;
    if (worker_.joinable())
    {
      worker_.join();
    }
  }

private:
  void Run()
  {
    pollfd listener{listener_fd_, POLLIN, 0};
    if (::poll(&listener, 1, 2000) <= 0)
    {
      return;
    }
    const int peer = ::accept(listener_fd_, nullptr, nullptr);
    if (peer < 0)
    {
      return;
    }
    accepted_ = true;
    pollfd client{peer, POLLIN, 0};
    if (::poll(&client, 1, 2000) > 0)
    {
      std::uint8_t hello[4096]{};
      received_client_hello_ = ::recv(peer, hello, sizeof(hello), 0) > 0;
    }
    if (received_client_hello_)
    {
      // The client must terminate the still-open socket after its deadline.
      do
      {
        if (::poll(&client, 1, 50) > 0)
        {
          std::uint8_t byte = 0u;
          peer_closed_ = ::recv(peer, &byte, 1, 0) == 0;
          if (peer_closed_)
          {
            break;
          }
        }
      } while (!stop_requested_);
    }
    ::close(peer);
  }

  int listener_fd_{-1};
  std::uint16_t port_{0u};
  std::atomic<bool> accepted_{false};
  std::atomic<bool> received_client_hello_{false};
  std::atomic<bool> peer_closed_{false};
  std::atomic<bool> stop_requested_{false};
  std::thread worker_{};
};

}  // namespace universal_gnss_transport::test
