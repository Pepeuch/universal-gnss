// SPDX-License-Identifier: LGPL-3.0-only
#include "esp_https_server.h"
// IDF 6.1's C macro omits its deprecated use_secure_element member. C aggregate
// initialization supplies zero, without suppressing C++ missing-field diagnostics.
httpd_ssl_config_t ug_https_defaults(void)
{
  const httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  return config;
}
