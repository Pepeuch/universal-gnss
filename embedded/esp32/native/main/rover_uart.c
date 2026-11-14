// SPDX-License-Identifier: LGPL-3.0-only
#include "driver/uart.h"
#include "espnow_rtcm.h"
#include "ug_portable.h"
static int uart_write(const uint8_t* bytes, size_t size, uint32_t wait_ms, void* context)
{
  (void)wait_ms;
  (void)context;
  // No TX ring: the documented nonblocking FIFO API is required here.
  return uart_tx_chars(UART_NUM_1, (const char*)bytes, (uint32_t)size);
}
esp_err_t ug_rover_uart_start(int tx, uint32_t baud)
{
  ug_portable_t output = ug_portable_default(UG_BOARD_C3);
  output.output = UG_OUTPUT_UART;
  output.tx = tx;
  output.baud = baud;
  if (tx < 0 || tx > 48 || !ug_portable_valid(&output))
    return ESP_ERR_INVALID_ARG;
  const uart_config_t config = {.baud_rate = baud,
                                .data_bits = UART_DATA_8_BITS,
                                .parity = UART_PARITY_DISABLE,
                                .stop_bits = UART_STOP_BITS_1,
                                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                                .source_clk = UART_SCLK_DEFAULT};
  esp_err_t result = uart_driver_install(UART_NUM_1, 256, 0, 0, NULL, 0);
  if (result != ESP_OK)
    return result;
  result = uart_param_config(UART_NUM_1, &config);
  if (result == ESP_OK)
    result =
        uart_set_pin(UART_NUM_1, tx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (result == ESP_OK)
    result = uart_disable_rx_intr(UART_NUM_1);  // TX-only: no unused RX interrupt work.
  if (result == ESP_OK)
    result = espnow_rtcm_set_rover_writer(uart_write, NULL);
  if (result != ESP_OK)
    uart_driver_delete(UART_NUM_1);
  return result;
}
