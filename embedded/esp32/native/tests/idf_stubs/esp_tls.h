#pragma once
#include <cstddef>
#include <sys/types.h>
#define ESP_OK 0
#define ESP_TLS_ERR_SSL_WANT_READ (-0x6900)
#define ESP_TLS_ERR_SSL_WANT_WRITE (-0x6880)
struct esp_tls
{
  int fd;
};
using esp_tls_t = esp_tls;
struct esp_tls_cfg_t
{
  bool non_block{}, is_plain_tcp{}, skip_common_name{};
  int timeout_ms{};
  const char* common_name{};
  int (*crt_bundle_attach)(void*){};
  const unsigned char *cacert_buf{}, *clientcert_buf{}, *clientkey_buf{};
  unsigned cacert_bytes{}, clientcert_bytes{}, clientkey_bytes{};
};
esp_tls_t* esp_tls_init();
int esp_tls_conn_new_async(const char*, int, int, const esp_tls_cfg_t*, esp_tls_t*);
int esp_tls_get_conn_sockfd(esp_tls_t*, int*);
ssize_t esp_tls_conn_read(esp_tls_t*, void*, std::size_t);
ssize_t esp_tls_conn_write(esp_tls_t*, const void*, std::size_t);
int esp_tls_conn_destroy(esp_tls_t*);

using esp_tls_error_handle_t = void*;
int esp_tls_get_error_handle(esp_tls_t*, esp_tls_error_handle_t*);
int esp_tls_get_and_clear_last_error(esp_tls_error_handle_t, int*, int*);
