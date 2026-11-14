// SPDX-License-Identifier: LGPL-3.0-only
// SDK API seam tests: these do not claim a real ESP TLS handshake.
#include "esp_tls.h"
#include "nvs.h"
#include "universal_gnss_transport/tcp_client_transport.hpp"
#include <cassert>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
static std::string stored_pem = "fake-cert";
static bool nvs_fail = false;
esp_err_t nvs_open(const char* ns, int mode, nvs_handle_t* out)
{
  assert(std::strcmp(ns, "ug_tls") == 0 && mode == NVS_READONLY);
  *out = 1;
  return nvs_fail ? -1 : ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* data, std::size_t* size)
{
  if (std::strcmp(key, "ca") != 0)
    return ESP_ERR_NVS_NOT_FOUND;
  if (!data)
  {
    *size = stored_pem.size();
    return ESP_OK;
  }
  if (*size < stored_pem.size())
    return -1;
  std::memcpy(data, stored_pem.data(), stored_pem.size());
  *size = stored_pem.size();
  return ESP_OK;
}
void nvs_close(nvs_handle_t)
{
}
using namespace universal_gnss_transport;
static int verification_flags = 0;
static int next_fd = -1, next_result = 1;
static bool force_wait = false, expect_tls = true;
esp_tls_t* esp_tls_init()
{
  return new esp_tls_t{-1};
}
int esp_tls_conn_new_async(
    const char* name, int len, int port, const esp_tls_cfg_t* cfg, esp_tls_t* t)
{
  assert(std::string(name, len) == "fixture.invalid" && port == 2101);
  assert(cfg->non_block && cfg->timeout_ms > 0 && !cfg->skip_common_name);
  assert(cfg->is_plain_tcp == !expect_tls);
  if (expect_tls)
    assert(cfg->crt_bundle_attach || cfg->cacert_buf);
  if (next_result == 1)
  {
    t->fd = next_fd;
    next_fd = -1;
  }
  return next_result;
}
int esp_tls_get_conn_sockfd(esp_tls_t* t, int* fd)
{
  *fd = t->fd;
  return 0;
}
ssize_t esp_tls_conn_read(esp_tls_t* t, void* data, std::size_t size)
{
  return force_wait ? ESP_TLS_ERR_SSL_WANT_READ : ::recv(t->fd, data, size, MSG_DONTWAIT);
}
ssize_t esp_tls_conn_write(esp_tls_t* t, const void* data, std::size_t size)
{
  return ::send(t->fd, data, size, MSG_DONTWAIT);
}
int esp_tls_conn_destroy(esp_tls_t* t)
{
  if (t->fd >= 0)
    ::close(t->fd);
  delete t;
  return 0;
}
int esp_tls_get_error_handle(esp_tls_t*, esp_tls_error_handle_t* handle)
{
  *handle = &verification_flags;
  return 0;
}
int esp_tls_get_and_clear_last_error(esp_tls_error_handle_t, int* code, int* flags)
{
  *code = 0;
  *flags = verification_flags;
  return 0;
}
static int Pair()
{
  int fds[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  next_fd = fds[0];
  return fds[1];
}
int main()
{
  signal(SIGPIPE, SIG_IGN);
  TcpClientConfig cfg;
  cfg.host = "fixture.invalid";
  cfg.port = 2101;
  cfg.tls_enabled = true;
  cfg.connect_timeout_ms = 5;
  cfg.read_timeout_ms = 2;
  cfg.write_timeout_ms = 2;
  TcpClientTransport t;
  char buffer[16];
  cfg.tls_verify_peer = false;
  assert(t.Open(cfg) == TransportError::kTlsVerificationFailure);
  cfg.tls_verify_peer = true;
  cfg.tls_ca_file = "/nonexistent-trust-bundle";
  assert(t.Open(cfg) == TransportError::kTlsVerificationFailure);
  cfg.tls_ca_file = "nvs://ug_tls/../ca";
  assert(t.Open(cfg) == TransportError::kTlsVerificationFailure);
  cfg.tls_ca_file = "nvs://ug_tls/ca";
  nvs_fail = true;
  assert(t.Open(cfg) == TransportError::kTlsVerificationFailure);
  nvs_fail = false;
  stored_pem = std::string(4097, 'x');
  assert(t.Open(cfg) == TransportError::kTlsVerificationFailure);
  stored_pem = std::string("key\0tail", 8);
  assert(t.Open(cfg) == TransportError::kTlsVerificationFailure);
  stored_pem = "fake-cert";
  int ca_peer = Pair();
  assert(t.Open(cfg) == TransportError::kNone);
  t.Close();
  ::close(ca_peer);
  cfg.tls_ca_file.clear();
  cfg.tls_client_private_key_file = "missing-key";
  assert(t.Open(cfg) == TransportError::kInvalidArgument);
  cfg.tls_client_private_key_file.clear();
  cfg.connect_cancelled = [] { return true; };
  assert(t.Open(cfg) == TransportError::kClosed);
  cfg.connect_cancelled = {};
  next_result = 0;
  assert(t.Open(cfg) == TransportError::kTimeout);
  next_result = -1;
  assert(t.Open(cfg) == TransportError::kTlsHandshakeFailure);
  verification_flags = 1;
  assert(t.Open(cfg) == TransportError::kTlsVerificationFailure);
  verification_flags = 0;
  next_result = 1;
  int peer = Pair();
  assert(t.Open(cfg) == TransportError::kNone);
  assert(t.Read(reinterpret_cast<std::uint8_t*>(buffer), sizeof(buffer)).bytes_read ==
         0);  // bounded idle
  assert(t.Read(nullptr, 1).error == TransportError::kInvalidArgument);
  assert(t.Read(reinterpret_cast<std::uint8_t*>(buffer), static_cast<std::size_t>(INT_MAX) + 1)
             .error == TransportError::kInvalidArgument);
  assert(send(peer, "abc", 3, 0) == 3);
  auto r = t.Read(reinterpret_cast<std::uint8_t*>(buffer), sizeof(buffer));
  assert(r.bytes_read == 3 && std::memcmp(buffer, "abc", 3) == 0);
  auto w = t.Write(reinterpret_cast<const std::uint8_t*>("def"), 3);
  assert(w.bytes_written == 3 && recv(peer, buffer, 3, 0) == 3 &&
         std::memcmp(buffer, "def", 3) == 0);
  assert(t.metrics().bytes_read == 3 && t.metrics().bytes_written == 3);
  close(peer);
  r = t.Read(reinterpret_cast<std::uint8_t*>(buffer), sizeof(buffer));
  assert(r.status == TransportStatus::kEndOfStream && !t.IsOpen());
  peer = Pair();
  assert(t.Open(cfg) == TransportError::kNone);
  close(t.native_fd());
  force_wait = true;
  r = t.Read(reinterpret_cast<std::uint8_t*>(buffer), sizeof(buffer));
  assert(r.status == TransportStatus::kError && r.error == TransportError::kReadFailure);
  force_wait = false;
  close(peer);
  peer = Pair();
  expect_tls = false;
  cfg.tls_enabled = false;
  cfg.tls_ca_file = "nvs://ug_tls/missing";
  cfg.tls_client_private_key_file = "unused-key";
  nvs_fail = true;
  assert(t.Open(cfg) == TransportError::kNone);
  t.Close();
  close(peer);
  nvs_fail = false;
  cfg.tls_ca_file.clear();
  cfg.tls_client_private_key_file.clear();
  peer = Pair();
  const int adopted = next_fd;
  next_fd = -1;
  cfg.tls_enabled = false;
  assert(t.AdoptConnectedSocket(adopted, cfg) == TransportError::kNone);
  close(peer);
  w = t.Write(reinterpret_cast<const std::uint8_t*>("no"), 2);
  assert(w.status == TransportStatus::kError && w.bytes_written == 0);
  t.Close();
  assert(t.Read(nullptr, 0).status == TransportStatus::kClosed);
}
