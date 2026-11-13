// SPDX-License-Identifier: LGPL-3.0-only
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ug_board.hpp"
#include "ug_commission.hpp"
#include "ug_configuration.hpp"
#include "ug_gateway_service.hpp"
#include "ug_services.hpp"
#include "ug_wait.hpp"
#include "ug_web.hpp"
#include "ug_wifi.hpp"
extern "C" {
#include "espnow_rtcm.h"
#include "ug_ethernet.h"
}
#if UG_LOCAL_GNSS
#include "ug_gnss_service.hpp"
#endif
namespace
{
void NetworkEvent(void*, esp_event_base_t base, std::int32_t event, void*)
{
  if (base == ETH_EVENT && event == ETHERNET_EVENT_DISCONNECTED)
    ug::embedded::SetNetworkLink(ug::embedded::NetworkLink::kEthernet, false);
  if (base == IP_EVENT && event == IP_EVENT_ETH_LOST_IP)
    ug::embedded::SetNetworkLink(ug::embedded::NetworkLink::kEthernet, false);
  if (base == IP_EVENT && event == IP_EVENT_ETH_GOT_IP)
    ug::embedded::SetNetworkLink(ug::embedded::NetworkLink::kEthernet, true);
}
bool RadioDestination(const ug::embedded::Gateway::Frame& frame, void*)
{
  return ug::embedded::Configuration()->radio_enabled &&
         ug::embedded::CorrectionSourceCurrent(frame.source, frame.incarnation) &&
         espnow_rtcm_send_frame_at(frame.bytes.data(),
                                   frame.size,
                                   static_cast<uint32_t>(frame.receipt_ns / 1000000)) == ESP_OK;
}
bool radio_initialized;
void RadioControl(void*)
{
  bool active = true;
  for (;;)
  {
    const bool desired = radio_initialized && ug::embedded::Configuration()->radio_enabled;
    if (desired != active)
    {
      ug::embedded::SetCorrectionDestination(0, desired ? RadioDestination : nullptr, nullptr);
      if (!desired)
        espnow_rtcm_invalidate_pending();
      active = desired;
    }
    vTaskDelay(ug::embedded::FiniteWaitTicks(20, configTICK_RATE_HZ));
  }
}
}  // namespace
extern "C" void app_main()
{
  using namespace ug::embedded;
  if (nvs_flash_init() != ESP_OK)
    ESP_LOGE("UG", "NVS initialization failed; originals preserved, no automatic erase");
  if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK)
  {
    ESP_LOGE("UG", "Network core initialization failed");
    return;
  }
#if CONFIG_IDF_TARGET_ESP32S3
  Capabilities capabilities{UG_LOCAL_GNSS, true, false, 5, "waveshare-s3-eth", UG_PROFILE_NAME};
  capabilities.gnss_pin_mask = GnssPinsForBoard(capabilities.board);
  const ug_ethernet_pins_t ethernet_pins{11, 12, 13, 14, 10, 9};
#else
  // Experimental wiring, compile-qualified only; GPIO18/19 USB remains reserved.
  Capabilities capabilities{false, false, false, 1, "supermini-c3", UG_PROFILE_NAME};
  const ug_ethernet_pins_t ethernet_pins{4, 5, 6, 7, 10, 3};
#endif
  std::string error;
  if (!StartConfiguration(capabilities, error))
    ESP_LOGE("UG", "Configuration locked: %s", error.c_str());
  if (StartGatewayService() != ESP_OK || StartNetworkServices() != ESP_OK)
  {
    ESP_LOGE("UG", "Gateway services could not start");
    return;
  }
  esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, NetworkEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, NetworkEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, NetworkEvent, nullptr);
  if (ug_ethernet_start_w5500(&ethernet_pins) != ESP_OK)
    ESP_LOGE("UG", "Ethernet unavailable; local corrections remain available");
  if (StartWifiAndClock() != ESP_OK)
    ESP_LOGE("UG", "Wi-Fi/time setup failed; Ethernet remains available");
  // No STA association occurs unless explicitly enabled; radio+STA conflicts are rejected.
  if (Configuration()->radio_enabled)
  {
    const auto radio_config = Configuration();
    if (espnow_rtcm_configure(radio_config->radio_peer.c_str(), radio_config->radio_channel) ==
            ESP_OK &&
        espnow_rtcm_start() == ESP_OK)
    {
      radio_initialized = true;
      SetCorrectionDestination(0, RadioDestination, nullptr);
    }
    else
      ESP_LOGE("UG", "ESP-NOW unavailable; wired services remain available");
  }
#if UG_LOCAL_GNSS
  if (StartGnssService() != ESP_OK)
    ESP_LOGE("UG", "GNSS task allocation failed");
#endif
  if (xTaskCreate(RadioControl, "ug_radio_control", 3072, nullptr, 3, nullptr) != pdPASS)
    ESP_LOGE("UG", "Radio control task unavailable");
  if (StartLocalCommissioning(RestartNetworkServices) != ESP_OK)
    ESP_LOGE("UG", "Local commissioning unavailable");
  if (StartWebService() != ESP_OK)
    ESP_LOGW("UG", "HTTPS unavailable: provision TLS identity locally, then reboot");
}
