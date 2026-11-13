// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_commission.hpp"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "ug_configuration.hpp"
#include "ug_credentials.hpp"
#include "ug_json.hpp"
#include <array>
#include <cstdio>
#include <cstring>
namespace ug::embedded
{
namespace
{
// Single local operator owner. Never linked into the rover's binary RTCM path.
std::array<char, 8192 + 32> line{};
void (*tls_changed)() = nullptr;
void Handle(std::string_view command)
{
  std::string error;
  bool ok = false;
  if (command.substr(0, 7) == "config ")
    ok = PatchConfiguration(command.substr(7), error);
  else if (command.substr(0, 9) == "tls-init ")
  {
    auto root = ParseBoundedObject(command.substr(9), error);
    if (root && OnlyKeys(root.get(), {"certificate_pem", "private_key_pem", "confirm"}))
    {
      auto* cert = cJSON_GetObjectItemCaseSensitive(root.get(), "certificate_pem");
      auto* key = cJSON_GetObjectItemCaseSensitive(root.get(), "private_key_pem");
      auto* confirm = cJSON_GetObjectItemCaseSensitive(root.get(), "confirm");
      if (cJSON_IsString(cert) && cJSON_IsString(key) && cJSON_IsString(confirm) &&
          std::strcmp(confirm->valuestring, "REPLACE_TLS_IDENTITY") == 0)
        ok = StoreWebIdentity({cert->valuestring, key->valuestring}, error);
    }
  }
  else if (command.substr(0, 11) == "tls-object ")
  {
    ok = ApplyTlsObject(command.substr(11), error);
    if (ok && tls_changed)
      tls_changed();
  }
  else if (command == "reboot CONFIRM")
  {
    std::puts("UG_LOCAL restarting");
    std::fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
  }
  else
    error = "expected config/tls-init/tls-object <JSON> or reboot CONFIRM";
  // Report only a status/reason, never the input or secret material.
  std::printf("UG_LOCAL %s%s%s\n", ok ? "OK" : "ERROR", error.empty() ? "" : ": ", error.c_str());
  std::fflush(stdout);
}
void Console(void*)
{
  std::size_t size = 0;
  bool overflow = false;
  for (;;)
  {
    const int c = std::getchar();
    if (c == EOF)
    {
      std::clearerr(stdin);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (c == '\n')
    {
      if (!overflow && size)
        Handle({line.data(), size});
      else if (overflow)
        std::puts("UG_LOCAL ERROR: line budget exceeded");
      mbedtls_platform_zeroize(line.data(), line.size());
      size = 0;
      overflow = false;
    }
    else if (c != '\r')
    {
      if (c == 0 || size >= line.size() - 1)
        overflow = true;
      else if (!overflow)
        line[size++] = static_cast<char>(c);
    }
  }
}
}  // namespace
esp_err_t StartLocalCommissioning(void (*credentials_changed)())
{
  tls_changed = credentials_changed;
  return xTaskCreate(Console, "ug_local_setup", 16384, nullptr, 1, nullptr) == pdPASS
             ? ESP_OK
             : ESP_ERR_NO_MEM;
}
}  // namespace ug::embedded
