// SPDX-License-Identifier: LGPL-3.0-only
// ESP-IDF backend for the existing UG transport contract and actual NtripClient.
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "mbedtls/platform_util.h"
#include "universal_gnss_transport/tcp_client_transport.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <unistd.h>
namespace universal_gnss_transport
{
namespace
{
constexpr std::uint32_t kDefaultTimeoutMs = 5000;
bool LoadPem(const std::string& path, std::string& destination)
{
  if (path.empty())
    return true;
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file)
    return false;
  std::array<char, 512> chunk{};
  destination.clear();
  bool ok = true;
  while (const auto size = std::fread(chunk.data(), 1, chunk.size(), file))
  {
    if (destination.size() + size > 65536)
    {
      ok = false;
      break;
    }
    destination.append(chunk.data(), size);
  }
  ok = ok && !std::ferror(file);
  std::fclose(file);
  ok = ok && !destination.empty();
  if (ok)
    destination.push_back('\0');
  return ok;
}
bool WantIo(const ssize_t value)
{
  return value == ESP_TLS_ERR_SSL_WANT_READ || value == ESP_TLS_ERR_SSL_WANT_WRITE ||
         (value < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR));
}
enum class IoWait
{
  Ready,
  Timeout,
  Failed
};
IoWait Wait(const int fd, const bool writing, const std::int64_t deadline_us)
{
  while (esp_timer_get_time() < deadline_us)
  {
    const auto remain = deadline_us - esp_timer_get_time();
    if (remain <= 0)
      return IoWait::Timeout;
    timeval timeout{static_cast<time_t>(remain / 1000000),
                    static_cast<suseconds_t>(remain % 1000000)};
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(fd, &ready);
    const int result =
        select(fd + 1, writing ? nullptr : &ready, writing ? &ready : nullptr, nullptr, &timeout);
    if (result > 0)
      return IoWait::Ready;
    if (result == 0)
      return IoWait::Timeout;
    if (errno != EINTR)
      return IoWait::Failed;
  }
  return IoWait::Timeout;
}
}  // namespace
TcpClientTransport::TcpClientTransport(const TcpClientConfig& config) : config_(config)
{
}
TcpClientTransport::~TcpClientTransport()
{
  Close();
}
TransportError TcpClientTransport::Open(const TcpClientConfig& config)
{
  Close();
  config_ = config;
  const auto failed = [&](const TransportError error) {
    Close();
    metrics_.last_error = error;
    return error;
  };
  if (config.host.empty() || config.host.size() > 253 || config.port == 0)
    return failed(TransportError::kInvalidArgument);
  if (config.tls_enabled && !config.tls_verify_peer)
    return failed(TransportError::kTlsVerificationFailure);  // No insecure production fallback.
  if ((!config.tls_client_certificate_file.empty()) !=
      (!config.tls_client_private_key_file.empty()))
    return failed(TransportError::kInvalidArgument);
  if (!LoadPem(config.tls_ca_file, ca_pem_) ||
      !LoadPem(config.tls_client_certificate_file, client_pem_) ||
      !LoadPem(config.tls_client_private_key_file, key_pem_))
    return failed(TransportError::kTlsVerificationFailure);
  idf_tls_ = esp_tls_init();
  if (!idf_tls_)
    return failed(TransportError::kConnectFailure);
  esp_tls_cfg_t tls{};
  tls.non_block = true;
  tls.timeout_ms = static_cast<int>(std::min<std::uint32_t>(
      config.connect_timeout_ms ? config.connect_timeout_ms : kDefaultTimeoutMs, INT_MAX));
  tls.is_plain_tcp = !config.tls_enabled;
  if (config.tls_enabled)
  {
    tls.skip_common_name = false;
    if (!config.tls_server_name.empty())
      tls.common_name = config.tls_server_name.c_str();
    if (ca_pem_.empty())
      tls.crt_bundle_attach = esp_crt_bundle_attach;
    else
    {
      tls.cacert_buf = reinterpret_cast<const unsigned char*>(ca_pem_.data());
      tls.cacert_bytes = ca_pem_.size();
    }
    if (!client_pem_.empty())
    {
      tls.clientcert_buf = reinterpret_cast<const unsigned char*>(client_pem_.data());
      tls.clientcert_bytes = client_pem_.size();
      tls.clientkey_buf = reinterpret_cast<const unsigned char*>(key_pem_.data());
      tls.clientkey_bytes = key_pem_.size();
    }
  }
  const auto deadline = esp_timer_get_time() + static_cast<std::int64_t>(tls.timeout_ms) * 1000;
  while (esp_timer_get_time() < deadline)
  {
    if (config.connect_cancelled && config.connect_cancelled())
      return failed(TransportError::kClosed);
    const int result = esp_tls_conn_new_async(
        config.host.c_str(), config.host.size(), config.port, &tls, idf_tls_);
    if (result == 1)
    {
      if (esp_tls_get_conn_sockfd(idf_tls_, &fd_) != ESP_OK || fd_ < 0 || fd_ >= FD_SETSIZE)
        return failed(TransportError::kConnectFailure);
      if (config.tcp_nodelay)
      {
        int enabled = 1;
        if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) != 0)
          return failed(TransportError::kConnectFailure);
      }
      NoteReconnect(metrics_);
      ClearLastTransportError(metrics_);
      return TransportError::kNone;
    }
    if (result < 0)
    {
      esp_tls_error_handle_t handle = nullptr;
      int code = 0, flags = 0;
      if (config.tls_enabled && esp_tls_get_error_handle(idf_tls_, &handle) == ESP_OK && handle)
        (void)esp_tls_get_and_clear_last_error(handle, &code, &flags);
      return failed(!config.tls_enabled ? TransportError::kConnectFailure
                    : flags             ? TransportError::kTlsVerificationFailure
                                        : TransportError::kTlsHandshakeFailure);
    }
    vTaskDelay(pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1);
  }
  return failed(TransportError::kTimeout);
}
TransportError TcpClientTransport::AdoptConnectedSocket(const int fd, const TcpClientConfig& config)
{
  Close();
  config_ = config;
  if (fd < 0)
    return TransportError::kInvalidArgument;
  if (config.tls_enabled || fd >= FD_SETSIZE)
  {
    ::close(fd);
    return TransportError::kUnsupported;
  }
  fd_ = fd;
  use_generic_fd_io_ = true;
  const auto flags = fcntl(fd_, F_GETFL, 0);
  if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0)
  {
    Close();
    return TransportError::kConnectFailure;
  }
  ClearLastTransportError(metrics_);
  return TransportError::kNone;
}
ReadResult TcpClientTransport::Read(std::uint8_t* destination, const std::size_t capacity)
{
  if (!IsOpen())
    return {0, TransportStatus::kClosed, TransportError::kClosed};
  if ((!destination && capacity) || capacity > INT_MAX)
    return {0, TransportStatus::kError, TransportError::kInvalidArgument};
  if (!capacity)
    return {};
  const auto deadline = esp_timer_get_time() +
                        static_cast<std::int64_t>(config_.read_timeout_ms ? config_.read_timeout_ms
                                                                          : kDefaultTimeoutMs) *
                            1000;
  for (;;)
  {
    errno = 0;
    const auto read = idf_tls_ ? esp_tls_conn_read(idf_tls_, destination, capacity)
                               : recv(fd_, destination, capacity, MSG_DONTWAIT);
    if (read > 0)
    {
      NoteReadBytes(metrics_, read);
      return {static_cast<std::size_t>(read), TransportStatus::kOk, TransportError::kNone};
    }
    if (read == 0)
    {
      Close();
      return {0, TransportStatus::kEndOfStream, TransportError::kNone};
    }
    if (WantIo(read))
    {
      if (config_.nonblocking)
        return {};
      const auto waited = Wait(fd_, read == ESP_TLS_ERR_SSL_WANT_WRITE, deadline);
      if (waited == IoWait::Ready)
        continue;
      if (waited == IoWait::Timeout)
        return {};
      NoteReadError(metrics_, TransportError::kReadFailure);
      Close();
      return {0, TransportStatus::kError, TransportError::kReadFailure};
    }
    NoteReadError(metrics_, TransportError::kReadFailure);
    Close();
    return {0, TransportStatus::kError, TransportError::kReadFailure};
  }
}
WriteResult TcpClientTransport::Write(const std::uint8_t* data, const std::size_t size)
{
  if (!IsOpen())
    return {0, TransportStatus::kClosed, TransportError::kClosed};
  if ((!data && size) || size > INT_MAX)
    return {0, TransportStatus::kError, TransportError::kInvalidArgument};
  if (!size)
    return {};
  const auto deadline =
      esp_timer_get_time() + static_cast<std::int64_t>(config_.write_timeout_ms
                                                           ? config_.write_timeout_ms
                                                           : kDefaultTimeoutMs) *
                                 1000;
  for (;;)
  {
    errno = 0;
    const auto written =
        idf_tls_ ? esp_tls_conn_write(idf_tls_, data, size) : send(fd_, data, size, MSG_DONTWAIT);
    if (written > 0)
    {
      NoteWrittenBytes(metrics_, written);
      return {static_cast<std::size_t>(written), TransportStatus::kOk, TransportError::kNone};
    }
    auto error = TransportError::kWriteFailure;
    if (written < 0 && WantIo(written))
    {
      if (config_.nonblocking)
        error = TransportError::kTimeout;
      else
      {
        const auto waited = Wait(fd_, written != ESP_TLS_ERR_SSL_WANT_READ, deadline);
        if (waited == IoWait::Ready)
          continue;
        error =
            waited == IoWait::Timeout ? TransportError::kTimeout : TransportError::kWriteFailure;
      }
    }
    NoteWriteError(metrics_, error);
    return {0, TransportStatus::kError, error};
  }
}
bool TcpClientTransport::IsOpen() const
{
  return fd_ >= 0;
}
void TcpClientTransport::Close()
{
  if (idf_tls_)
  {
    esp_tls_conn_destroy(idf_tls_);
    idf_tls_ = nullptr;
  }
  else if (fd_ >= 0)
    ::close(fd_);
  fd_ = -1;
  use_generic_fd_io_ = false;
  // Do not log credential material; best-effort overwrite of owned key bytes.
  mbedtls_platform_zeroize(key_pem_.data(), key_pem_.size());
  ca_pem_.clear();
  client_pem_.clear();
  key_pem_.clear();
}
int TcpClientTransport::native_fd() const
{
  return fd_;
}
const TcpClientConfig& TcpClientTransport::config() const
{
  return config_;
}
const TransportMetrics& TcpClientTransport::metrics() const
{
  return metrics_;
}
}  // namespace universal_gnss_transport
