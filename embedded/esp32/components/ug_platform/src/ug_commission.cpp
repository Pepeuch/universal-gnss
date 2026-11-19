// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_commission.hpp"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "ug_configuration.hpp"
#include "ug_console_buffer.hpp"
#include "ug_credentials.hpp"
#include "ug_json.hpp"
#include "ug_memory_diagnostics.hpp"
#include "ug_s3_json_memory.hpp"
#include "ug_s3_memory_options.hpp"
#include "ug_web_auth_platform.hpp"
#if CONFIG_IDF_TARGET_ESP32S3
#include "esp_mac.h"
#include "ug_pairing_read.hpp"
extern "C" {
#include "espnow_rtcm.h"
}
#endif
#include <charconv>
#if CONFIG_IDF_TARGET_ESP32S3 && UG_S3_CONSOLE_PSRAM
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#endif
#include "ug_portable_settings.hpp"
#include "ug_supervisor.hpp"
#include <array>
#include <cstdio>
#include <cstring>
namespace ug::embedded
{
namespace
{
// Single local operator owner. Never linked into the rover's binary RTCM path.
#if CONFIG_IDF_TARGET_ESP32S3 && UG_S3_CONSOLE_PSRAM
ConsoleBuffer<8192 + 32> line;
#else
std::array<char, 8192 + 32> line{};
#endif
void (*tls_changed)() = nullptr;
#if CONFIG_IDF_TARGET_ESP32S3
GatewayPairingRead pairing_read;
bool PairingReadReply(std::string_view command)
{
  if (!pairing_read.Query(command, esp_timer_get_time()))
    return false;
  std::uint8_t mac[6]{};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    return false;
  const auto config = Configuration();
  espnow_rtcm_stats_t radio{};
  espnow_rtcm_get_stats(&radio);
  const bool restart =
      config->radio_enabled &&
      (radio.tx_quarantined || !espnow_rtcm_configuration_matches(config->radio_peer.c_str(),
                                                                  config->radio_key.c_str(),
                                                                  config->radio_channel));
  // All strings are firmware-controlled literals or validated MAC addresses.
  // Never print a private key, credential, arbitrary command or configuration.
  std::printf("UGCFG/1 {\"firmware\":\"universal-gnss\",\"board\":\"esp32s3\","
              "\"hardware\":\"waveshare-s3-eth\",\"role\":\"%s\",\"mode\":\"control\","
              "\"nonce\":\"%s\",\"writable\":%s,\"maintenance_open\":true,"
              "\"sta_mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"configured_channel\":%u,"
              "\"peer\":\"%s\",\"key_set\":%s,\"enabled\":%s,\"wifi_sta\":%s,"
              "\"started\":%s,\"effective_channel\":%u,\"restart_required\":%s,"
              "\"proves_rover_delivery\":false}\n",
              BoardCapabilities().role.c_str(),
              pairing_read.nonce(),
              ConfigurationWritable() ? "true" : "false",
              mac[0],
              mac[1],
              mac[2],
              mac[3],
              mac[4],
              mac[5],
              config->radio_channel,
              config->radio_peer.c_str(),
              config->radio_key.empty() ? "false" : "true",
              config->radio_enabled ? "true" : "false",
              config->wifi_sta_enabled ? "true" : "false",
              radio.started ? "true" : "false",
              radio.effective_channel,
              restart ? "true" : "false");
  return true;
}
#endif
void Handle(std::string_view command)
{
  std::string error;
  bool ok = false;
  if (command.substr(0, 7) == "UGCFG/1")
  {
#if CONFIG_IDF_TARGET_ESP32S3
    ok = PairingReadReply(command);
#endif
    if (!ok)
      error = "invalid or expired local identity query";
  }
  else if (command == "status")
  {
    const auto services = ServiceHealths();
    const auto now = esp_timer_get_time() * 1000;
    std::printf("UG_LOCAL degraded=%d\n", ServicesDegraded(services, now));
    for (std::size_t i = 0; i < services.size(); ++i)
    {
      const auto& s = services[i];
      if (s.state == ServiceState::kDisabled)
        continue;
      std::printf(
          "UG_LOCAL service=%s state=%s error=%ld stale=%d watchdog=%d watchdog_error=%ld\n",
          ServiceName(static_cast<ServiceId>(i)),
          ServiceStateName(s.state),
          static_cast<long>(s.error),
          ServiceStale(s, now),
          s.watchdog_registered,
          static_cast<long>(s.watchdog_error));
    }
    const auto https = LastHttpsStartupStatus();
    std::printf("UG_LOCAL https_startup=%s sdk_error=%ld admin_configured=%d\n",
                HttpsStartupReasonName(https.reason),
                static_cast<long>(https.sdk_error),
#if CONFIG_IDF_TARGET_ESP32S3
                WebAdministratorConfigured());
#else
                !Configuration()->admin_token_sha256.empty());
#endif
    ok = true;
  }
  else if (command == "memory")
  {
    PrintMemoryDiagnostics();
    ok = true;
  }
  else if (command.substr(0, std::string_view("memory json-fail ").size()) == "memory json-fail ")
  {
    constexpr std::string_view prefix = "memory json-fail ";
    int budget{};
    const auto parsed =
        std::from_chars(command.data() + prefix.size(), command.data() + command.size(), budget);
    ok = parsed.ec == std::errc{} && parsed.ptr == command.data() + command.size() &&
         ArmS3JsonMemoryFailure(budget);
    if (!ok)
      error = "invalid or unavailable one-shot JSON fault budget";
  }
  else if (command == "memory json-large")
    ok = RunMemoryJsonStress(error);
  else if (command == "memory trace")
    ok = MemoryTraceCommand(true, error);
  else if (command == "memory trace-stop")
    ok = MemoryTraceCommand(false, error);
#if CONFIG_IDF_TARGET_ESP32S3
  else if (command == "auth-benchmark")
  {
    std::int64_t elapsed{};
    ok = BenchmarkWebPassword(elapsed);
    std::printf("UG_AUTH benchmark_fixed_vector=%s iterations=%u elapsed_us=%lld "
                "no_credentials_changed=1\n",
                ok ? "pass" : "fail",
                WebAuth::kIterations,
                static_cast<long long>(elapsed));
  }
#endif
  else if (command == "tls-status")
  {
    LoadedWebIdentity stored;
    const auto read = LoadWebIdentity(stored);
    const auto boot = LastHttpsStartupStatus();
    std::printf("UG_LOCAL tls_identity=%s sdk_error=%ld admin_configured=%d https_startup=%s\n",
                HttpsStartupReasonName(read.reason),
                static_cast<long>(read.sdk_error),
#if CONFIG_IDF_TARGET_ESP32S3
                WebAdministratorConfigured(),
#else
                !Configuration()->admin_token_sha256.empty(),
#endif
                HttpsStartupReasonName(boot.reason));
    if (read.ok())
    {
      std::array<std::uint8_t, 32> fingerprint{};
      if (WebCertificateFingerprint(stored, fingerprint))
      {
        std::printf("UG_LOCAL certificate_sha256=");
        for (const auto byte : fingerprint)
          std::printf("%02x", unsigned(byte));
        std::printf("\n");
      }
      else
        std::puts("UG_LOCAL certificate_sha256=unavailable");
    }
    ok = true;  // Read-only diagnostic succeeded even when no identity exists.
  }
  else if (command == "events")
  {
    const auto snapshot = ServiceEvents();
    std::printf("UG_LOCAL events=%zu overwritten=%llu rejected=%llu (volatile this boot)\n",
                snapshot.count,
                static_cast<unsigned long long>(snapshot.overwritten),
                static_cast<unsigned long long>(snapshot.rejected));
    for (std::size_t i = 0; i < snapshot.count; ++i)
    {
      const auto& event = snapshot.events[i];
      std::printf("UG_LOCAL seq=%llu uptime_ns=%lld service=%s state=%s kind=%s error=%ld\n",
                  static_cast<unsigned long long>(event.sequence),
                  static_cast<long long>(event.receipt_ns),
                  ServiceName(event.service),
                  ServiceStateName(event.state),
                  ServiceEventName(event.kind),
                  static_cast<long>(event.error));
      if (event.service == ServiceId::kHttps && event.state == ServiceState::kFailed)
        std::printf("UG_LOCAL https_event_reason=%s\n",
                    HttpsStartupReasonName(static_cast<HttpsStartupReason>(event.error)));
    }
    ok = true;
  }
#if CONFIG_IDF_TARGET_ESP32S3
  else if (command == "portable-export")
  {
    std::string json;
    ok = ExportPortableSettings(*Configuration(), BoardCapabilities(), json, error);
    if (ok)
      std::printf("UGPORT/1 JSON %s\n", json.c_str());
  }
  else if (command.substr(0, 15) == "portable-check ")
  {
    std::string preview;
    ok = PreviewPortableConfiguration(command.substr(15), preview, error);
    std::printf("UGPORT/1 %s\n", ok ? "VALID" : "ERROR");
  }
  else if (command.substr(0, 23) == "portable-apply CONFIRM ")
  {
    ok = ApplyPortableConfiguration(command.substr(23), error);
    std::printf("UGPORT/1 %s\n", ok ? "SAVED_RESTART_REQUIRED" : "ERROR");
  }
#endif
  else if (command.substr(0, 9) == "rollback ")
    ok = RollbackConfiguration(command.substr(9), error);
  else if (command == "history")
  {
    const auto history = ConfigurationBanks();
    std::printf("UG_LOCAL generation=%llu previous=%llu available=%d writable=%d\n",
                static_cast<unsigned long long>(history.generation),
                static_cast<unsigned long long>(history.previous_generation),
                history.previous_available,
                ConfigurationWritable());
    ok = true;
  }
  else if (command.substr(0, 7) == "config ")
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
    error = "expected status/history/events/tls-status, config/rollback/tls-init/tls-object <JSON> "
            "or reboot "
            "CONFIRM";
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
#if CONFIG_IDF_TARGET_ESP32S3 && UG_S3_CONSOLE_PSRAM
  if (!line.Initialize(
          [](std::size_t size) -> void* {
            return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
          },
          heap_caps_free,
          mbedtls_platform_zeroize))
    return ESP_ERR_NO_MEM;
  std::printf("UG_MEM console_buffer bytes=%zu external=%d\n",
              line.size(),
              esp_ptr_external_ram(line.data()));
#endif
  tls_changed = credentials_changed;
  const bool created = xTaskCreate(Console, "ug_local_setup", 16384, nullptr, 1, nullptr) == pdPASS;
#if CONFIG_IDF_TARGET_ESP32S3 && UG_S3_CONSOLE_PSRAM
  if (!created)
    line.Reset();
#endif
  return created ? ESP_OK : ESP_ERR_NO_MEM;
}
}  // namespace ug::embedded
