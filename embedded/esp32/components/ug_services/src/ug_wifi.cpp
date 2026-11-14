// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_wifi.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "mdns.h"
#include "ug_configuration.hpp"
#include "ug_services.hpp"
#include <atomic>
#include <cstring>
namespace ug::embedded
{
namespace
{
std::atomic<unsigned> links{0}, reconnect_attempt{0};
TimerHandle_t reconnect_timer;
bool station_enabled;
std::string ntp_server;  // lwIP borrows the server pointer until shutdown.
void Reconnect(TimerHandle_t)
{
  if (station_enabled)
    (void)esp_wifi_connect();
}
void Event(void*, esp_event_base_t base, std::int32_t id, void*)
{
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
  {
    reconnect_attempt = 0;
    SetNetworkLink(NetworkLink::kStation, true);
  }
  else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)
    SetNetworkLink(NetworkLink::kStation, false);
  else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
  {
    if (station_enabled)
      (void)esp_wifi_connect();
  }
  else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
  {
    SetNetworkLink(NetworkLink::kStation, false);
    const auto n = reconnect_attempt.load();
    if (n < 5)
      reconnect_attempt = n + 1;
    if (reconnect_timer)
    {
      xTimerChangePeriod(reconnect_timer, pdMS_TO_TICKS(1000U << (n > 5 ? 5 : n)), 0);
      xTimerStart(reconnect_timer, 0);
    }
  }
  else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START)
    SetNetworkLink(NetworkLink::kAccessPoint, true);
  else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STOP)
    SetNetworkLink(NetworkLink::kAccessPoint, false);
}
}  // namespace
void SetNetworkLink(NetworkLink link, bool available)
{
  const auto bit = 1U << static_cast<unsigned>(link);
  const auto previous = available ? links.fetch_or(bit) : links.fetch_and(~bit);
  const auto current = available ? (previous | bit) : (previous & ~bit);
  // Even a handover with another live interface invalidates old connection ownership.
  if (previous != current)
    NetworkAvailabilityChanged(current != 0);
}
esp_err_t StartWifiAndClock()
{
  const auto settings = Configuration();
  if (!settings->ntp_server.empty())
  {
    // UTC is used for certificate validity only, never correction freshness.
    ntp_server = settings->ntp_server;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp_server.c_str());
    const auto result = esp_netif_sntp_init(&config);
    if (result != ESP_OK)
      return result;
  }
  if (mdns_init() == ESP_OK)
  {
    mdns_hostname_set(settings->device_name.c_str());
    mdns_instance_name_set("Universal GNSS RTK");
    mdns_service_add(nullptr, "_https", "_tcp", 443, nullptr, 0);
  }
  if (!settings->wifi_sta_enabled && !settings->wifi_ap_enabled)
    return ESP_OK;
  station_enabled = settings->wifi_sta_enabled;
  if (settings->wifi_sta_enabled && settings->radio_enabled)
    return ESP_ERR_INVALID_ARG;
  if (settings->wifi_sta_enabled && !esp_netif_create_default_wifi_sta())
    return ESP_ERR_NO_MEM;
  if (settings->wifi_ap_enabled && !esp_netif_create_default_wifi_ap())
    return ESP_ERR_NO_MEM;
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  init.nvs_enable = false;
  auto result = esp_wifi_init(&init);
  if (result != ESP_OK)
    return result;
  if ((result = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK)
    return result;
  auto mode = settings->wifi_sta_enabled
                  ? (settings->wifi_ap_enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA)
                  : WIFI_MODE_AP;
  if ((result = esp_wifi_set_mode(mode)) != ESP_OK)
    return result;
  if (settings->wifi_sta_enabled)
  {
    wifi_config_t sta{};
    std::memcpy(sta.sta.ssid, settings->wifi_ssid.data(), settings->wifi_ssid.size());
    std::memcpy(sta.sta.password, settings->wifi_password.data(), settings->wifi_password.size());
    sta.sta.threshold.authmode =
        settings->wifi_password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    sta.sta.pmf_cfg.capable = true;
    if ((result = esp_wifi_set_config(WIFI_IF_STA, &sta)) != ESP_OK)
      return result;
    reconnect_timer =
        xTimerCreate("ug_wifi_retry", pdMS_TO_TICKS(1000), pdFALSE, nullptr, Reconnect);
    if (!reconnect_timer)
      return ESP_ERR_NO_MEM;
  }
  if (settings->wifi_ap_enabled)
  {
    wifi_config_t ap{};
    std::memcpy(ap.ap.ssid, settings->ap_ssid.data(), settings->ap_ssid.size());
    ap.ap.ssid_len = settings->ap_ssid.size();
    std::memcpy(ap.ap.password, settings->ap_password.data(), settings->ap_password.size());
    ap.ap.channel = settings->radio_channel;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.capable = true;
    if ((result = esp_wifi_set_config(WIFI_IF_AP, &ap)) != ESP_OK)
      return result;
  }
  if ((result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, Event, nullptr)) != ESP_OK)
    return result;
  if ((result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, Event, nullptr)) !=
      ESP_OK)
    return result;
  if ((result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, Event, nullptr)) !=
      ESP_OK)
    return result;
  return esp_wifi_start();
}
}  // namespace ug::embedded
