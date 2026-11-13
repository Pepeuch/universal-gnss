// SPDX-License-Identifier: LGPL-3.0-only
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ug_gateway.hpp"
extern "C" {
#include "espnow_rtcm.h"
}
#if UG_LOCAL_GNSS
#include "driver/uart.h"
#include "ug_receiver_runtime.hpp"
namespace
{
class UartSink final : public universal_gnss_transport::ByteSink
{
public:
  universal_gnss_transport::WriteResult Write(const std::uint8_t* data, std::size_t size) override
  {
    using namespace universal_gnss_transport;
    if ((!data && size) || size > 512 || !opened_)
      return {0, TransportStatus::kError, TransportError::kInvalidArgument};
    // Bounded driver TX buffer; the GNSS owner is the only writer.
    const auto sent = uart_write_bytes(UART_NUM_1, data, size);
    if (sent < 0)
      return {0, TransportStatus::kError, TransportError::kWriteFailure};
    return {static_cast<std::size_t>(sent), TransportStatus::kOk, TransportError::kNone};
  }
  bool IsOpen() const override
  {
    return opened_;
  }
  void Close() override
  {
    opened_ = false;
  }
  bool opened_{false};
};
UartSink uart_sink;
ug::embedded::Receiver receiver(uart_sink);
}  // namespace
#endif
namespace
{
ug::embedded::Gateway gateway;
ug::embedded::Gateway::Frame frame;
}  // namespace
extern "C" void app_main()
{
  if (nvs_flash_init() != ESP_OK || esp_netif_init() != ESP_OK ||
      esp_event_loop_create_default() != ESP_OK)
  {
    ESP_LOGE("UG", "Persistent/network initialization failed; no automatic erase");
    return;
  }
  if (espnow_rtcm_start() != ESP_OK)
  {
    ESP_LOGE("UG", "ESP-NOW initialization failed");
    return;
  }
  gateway.ConfigureSource(0, 1, UG_LOCAL_GNSS);
  gateway.ConfigureSink(0, true, 3000000000LL);
#if UG_LOCAL_GNSS
  uart_config_t config{};
  config.baud_rate = 115200;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  if (uart_driver_install(UART_NUM_1, 4096, 1024, 0, nullptr, 0) != ESP_OK ||
      uart_param_config(UART_NUM_1, &config) != ESP_OK ||
      uart_set_pin(UART_NUM_1, 38, 37, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
  {
    ESP_LOGE("UG", "GNSS UART initialization failed");
    return;
  }
  uart_sink.opened_ = true;
  std::uint8_t bytes[512];
#endif
  for (;;)
  {
#if UG_LOCAL_GNSS
    const auto size = uart_read_bytes(UART_NUM_1, bytes, sizeof(bytes), pdMS_TO_TICKS(10));
    const auto now = esp_timer_get_time() * 1000;
    if (size > 0)
    {
      receiver.Feed(bytes, size, now);
      gateway.Feed(0, receiver.incarnation(), bytes, size, now);
    }
    receiver.Step(now);
#endif
    while (gateway.Pop(0, esp_timer_get_time() * 1000, frame))
      (void)espnow_rtcm_send_frame(frame.bytes.data(), frame.size);
    vTaskDelay(1);
  }
}
