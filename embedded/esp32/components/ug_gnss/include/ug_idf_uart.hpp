// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "ug_settings.hpp"
#include "ug_wait.hpp"
#include "universal_gnss_transport/byte_stream.hpp"
namespace ug::embedded
{
using universal_gnss_transport::ByteDuplex;
using universal_gnss_transport::ReadResult;
using universal_gnss_transport::TransportError;
using universal_gnss_transport::TransportStatus;
using universal_gnss_transport::WriteResult;
class NativeGnssUart final : public ByteDuplex
{
public:
  ReadResult Read(std::uint8_t* data, std::size_t size) override
  {
    return ReadBatch(data, size, true);
  }
  ReadResult ReadBatch(std::uint8_t* data, std::size_t size, bool wait)
  {
    if (!opened || !data || !size)
      return {0, TransportStatus::kError, TransportError::kInvalidArgument};
    PollEvents();
    if (faulted)
      return {0, TransportStatus::kError, TransportError::kReadFailure};
    const auto n =
        uart_read_bytes(UART_NUM_1, data, size, wait ? FiniteWaitTicks(10, configTICK_RATE_HZ) : 0);
    PollEvents();  // Never feed a batch when the driver reports a discontinuity.
    if (n < 0)
    {
      ++read_failures;
      faulted = true;
    }
    if (faulted)
      return {0, TransportStatus::kError, TransportError::kReadFailure};
    return {static_cast<std::size_t>(n), TransportStatus::kOk, TransportError::kNone};
  }
  void PollEvents()
  {
    if (!events)
      return;
    // Data notifications can fill the SDK queue and hide a later error event.
    // A full queue is therefore itself ambiguous, even without a received OVF.
    if (uxQueueMessagesWaiting(events) >= kEventDepth)
    {
      ++event_saturations;
      faulted = true;
    }
    uart_event_t event;
    for (std::size_t i = 0; i < kEventDepth && xQueueReceive(events, &event, 0) == pdTRUE; ++i)
    {
      if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL)
      {
        ++overflows;
        faulted = true;
      }
      else if (event.type == UART_PARITY_ERR || event.type == UART_FRAME_ERR ||
               event.type == UART_BREAK)
      {
        ++line_errors;
        faulted = true;
      }
    }
  }
  bool TakeFault()
  {
    PollEvents();
    if (!faulted)
      return false;
    // Discard local partial bytes, without claiming a receiver-side cutoff.
    // A flush failure keeps the adapter failed; no ambiguous input is accepted.
    faulted = uart_flush_input(UART_NUM_1) != ESP_OK;
    return true;
  }
  WriteResult Write(const std::uint8_t* data, std::size_t size) override
  {
    if (!opened || (!data && size) || size > 512)
      return {0, TransportStatus::kError, TransportError::kInvalidArgument};
    const auto n = uart_write_bytes(UART_NUM_1, data, size);
    return n < 0 ? WriteResult{0, TransportStatus::kError, TransportError::kWriteFailure}
                 : WriteResult{
                       static_cast<std::size_t>(n), TransportStatus::kOk, TransportError::kNone};
  }
  bool IsOpen() const override
  {
    return opened;
  }
  void Close() override
  {
    opened = false;
    if (installed)
    {
      uart_driver_delete(UART_NUM_1);
      installed = false;
    }
    events = nullptr;
    faulted = false;
    if (tx >= 0)
      gpio_reset_pin(static_cast<gpio_num_t>(tx));
    if (rx >= 0)
      gpio_reset_pin(static_cast<gpio_num_t>(rx));
    tx = rx = -1;
  }
  bool Open(const Settings& settings, std::uint64_t allowed_pins)
  {
    Close();
    auto allowed = [&](int pin) {
      return pin >= 0 && pin < 64 && (allowed_pins & (UINT64_C(1) << pin));
    };
    if (!allowed(settings.gnss_tx_pin) || !allowed(settings.gnss_rx_pin) ||
        settings.gnss_tx_pin == settings.gnss_rx_pin ||
        !GPIO_IS_VALID_OUTPUT_GPIO(settings.gnss_tx_pin) ||
        !GPIO_IS_VALID_GPIO(settings.gnss_rx_pin))
      return false;
    uart_config_t config{};
    config.baud_rate = settings.gnss_baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(UART_NUM_1, 4096, 1024, kEventDepth, &events, 0) != ESP_OK)
      return false;
    installed = true;
    tx = settings.gnss_tx_pin;
    rx = settings.gnss_rx_pin;
    if (uart_param_config(UART_NUM_1, &config) != ESP_OK ||
        uart_set_pin(UART_NUM_1, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
    {
      Close();
      return false;
    }
    opened = true;
    return true;
  }
  static constexpr std::size_t kEventDepth = 32;
  QueueHandle_t events{};
  bool faulted{};
  std::uint64_t overflows{}, line_errors{}, event_saturations{}, read_failures{};
  bool opened{false}, installed{false};
  int tx{-1}, rx{-1};
};
}  // namespace ug::embedded
