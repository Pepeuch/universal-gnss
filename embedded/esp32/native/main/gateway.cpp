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
#include "ug_supervisor.hpp"
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
  ug::embedded::WorkerHealth health(ug::embedded::ServiceId::kRadioControl, 5000000000LL, true);
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
    health.Progress();
    vTaskDelay(ug::embedded::FiniteWaitTicks(20, configTICK_RATE_HZ));
  }
}
std::int32_t StartNetworkCore(void*)
{
  const auto result = esp_netif_init();
  return result == ESP_OK ? esp_event_loop_create_default() : result;
}
std::int32_t StartEthernet(void* context)
{
  auto result =
      esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, NetworkEvent, nullptr);
  if (result == ESP_OK)
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, NetworkEvent, nullptr);
  if (result == ESP_OK)
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, NetworkEvent, nullptr);
  return result == ESP_OK ? ug_ethernet_start_w5500(static_cast<ug_ethernet_pins_t*>(context))
                          : result;
}
std::int32_t StartRadio(void*)
{
  using namespace ug::embedded;
  const auto config = Configuration();
  auto result = espnow_rtcm_configure(config->radio_peer.c_str(), config->radio_channel);
  if (result == ESP_OK)
    result = espnow_rtcm_start();
  if (result != ESP_OK)
    return result;
  radio_initialized = true;
  return SetCorrectionDestination(0, RadioDestination, nullptr) ? ESP_OK : ESP_FAIL;
}
std::int32_t StartRadioControl(void*)
{
  using namespace ug::embedded;
  ExpectWorker(ServiceId::kRadioControl, 5000000000LL);
  return xTaskCreate(RadioControl, "ug_radio_control", 3072, nullptr, 3, nullptr) == pdPASS
             ? ESP_OK
             : ESP_ERR_NO_MEM;
}
}  // namespace
extern "C" void app_main()
{
  using namespace ug::embedded;
  const auto nvs = nvs_flash_init();
  if (nvs != ESP_OK)
    ESP_LOGE("UG", "NVS initialization failed; originals preserved, no automatic erase");
#if CONFIG_IDF_TARGET_ESP32S3
  Capabilities capabilities{UG_LOCAL_GNSS, true, false, 5, "waveshare-s3-eth", UG_PROFILE_NAME};
  capabilities.gnss_pin_mask = GnssPinsForBoard(capabilities.board);
  ug_ethernet_pins_t ethernet_pins{11, 12, 13, 14, 10, 9};
#else
  // Experimental wiring, compile-qualified only; GPIO18/19 USB remains reserved.
  Capabilities capabilities{false, false, false, 1, "supermini-c3", UG_PROFILE_NAME};
  ug_ethernet_pins_t ethernet_pins{4, 5, 6, 7, 10, 3};
#endif
  std::string error;
  // Publishes a validated defaults snapshot even when storage is unavailable.
  // Every subsequent service can therefore inspect configuration safely; writes
  // and receiver mutation remain locked by the existing persistence policy.
  const bool configured = StartConfiguration(capabilities, error);
  ObserveService(ServiceId::kConfiguration,
                 configured ? ServiceState::kRunning : ServiceState::kFailed,
                 configured ? ESP_OK : (nvs == ESP_OK ? ESP_FAIL : nvs));
  if (!configured)
    ESP_LOGE("UG", "Configuration locked: %s", error.c_str());
  constexpr auto network = ServiceBit(ServiceId::kNetworkCore);
  constexpr auto routing = ServiceBit(ServiceId::kRouting);
  const bool radio_enabled = Configuration()->radio_enabled;
  const BootStep steps[] = {
      {ServiceId::kNetworkCore, 0, StartNetworkCore},
      {ServiceId::kRouting, 0, [](void*) -> std::int32_t { return StartGatewayService(); }},
      {ServiceId::kNtripSetup,
       network | routing,
       [](void*) -> std::int32_t { return StartNetworkServices(); }},
      {ServiceId::kEthernet, network, StartEthernet, &ethernet_pins},
      {ServiceId::kWifi, network, [](void*) -> std::int32_t { return StartWifiAndClock(); }},
      {ServiceId::kRadio, network | routing, StartRadio, nullptr, radio_enabled},
      {ServiceId::kRadioControl,
       ServiceBit(ServiceId::kRadio),
       StartRadioControl,
       nullptr,
       radio_enabled},
#if UG_LOCAL_GNSS
      {ServiceId::kGnss, 0, [](void*) -> std::int32_t { return StartGnssService(); }},
#endif
      {ServiceId::kConsole,
       0,
       [](void*) -> std::int32_t { return StartLocalCommissioning(RestartNetworkServices); }},
      {ServiceId::kHttps, network, [](void*) -> std::int32_t { return StartWebService(); }}};
  const auto boot = RunBoot(steps, sizeof(steps) / sizeof(steps[0]), ObserveBoot);
  if (!boot.plan_valid)
    ObserveBoot(ServiceId::kNetworkCore, ServiceState::kFailed, ESP_ERR_INVALID_ARG, nullptr);
  // Driver/setup success is not proof of a network link or physical correction
  // delivery. Per-worker progress and actual flow are reported separately.
}
