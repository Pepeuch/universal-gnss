// SPDX-License-Identifier: LGPL-3.0-only
#include "driver/uart.h"
#include "espnow_rtcm.h"
#include <assert.h>
esp_err_t ug_rover_uart_start(void);
static unsigned step, fail_step, deleted;
static espnow_rtcm_write_fn writer;
static int Next(void)
{
  return ++step == fail_step ? ESP_FAIL : ESP_OK;
}
esp_err_t uart_driver_install(int uart, int rx, int tx, int count, void* queue, int flags)
{
  assert(uart == UART_NUM_1 && rx == 256 && tx == 0 && count == 0 && !queue && flags == 0);
  return Next();
}
esp_err_t uart_driver_delete(int uart)
{
  assert(uart == UART_NUM_1);
  ++deleted;
  return ESP_OK;
}
esp_err_t uart_param_config(int uart, const uart_config_t* config)
{
  assert(step == 1 && uart == UART_NUM_1 && config->baud_rate == 230400);
  assert(config->data_bits == UART_DATA_8_BITS && config->parity == UART_PARITY_DISABLE &&
         config->stop_bits == UART_STOP_BITS_1 && config->flow_ctrl == UART_HW_FLOWCTRL_DISABLE);
  return Next();
}
esp_err_t uart_set_pin(int uart, int tx, int rx, int rts, int cts)
{
  assert(step == 2 && uart == UART_NUM_1 && tx == 4);
  assert(rx == UART_PIN_NO_CHANGE && rts == UART_PIN_NO_CHANGE && cts == UART_PIN_NO_CHANGE);
  return Next();
}
esp_err_t uart_disable_rx_intr(int uart)
{
  assert(step == 3 && uart == UART_NUM_1);
  return Next();
}
esp_err_t espnow_rtcm_set_rover_writer(espnow_rtcm_write_fn callback, void* context)
{
  assert(step == 4 && !context && callback);
  const int result = Next();
  if (result == ESP_OK)
    writer = callback;
  return result;
}
int uart_tx_chars(int uart, const char* bytes, uint32_t size)
{
  assert(uart == UART_NUM_1 && size == 3 && bytes[0] == '\0' && bytes[1] == (char)0xd3 &&
         bytes[2] == '\n');
  return 2;  // Pump, not backend, owns subsequent partial-write retries.
}
int main(void)
{
  const uint8_t binary[] = {0, 0xd3, '\n'};
  assert(ug_rover_uart_start() == ESP_OK && step == 5 && writer && deleted == 0);
  assert(writer(binary, sizeof(binary), 20, NULL) == 2);
  for (fail_step = 1; fail_step <= 5; ++fail_step)
  {
    step = deleted = 0;
    writer = NULL;
    assert(ug_rover_uart_start() != ESP_OK && step == fail_step && !writer);
    assert(deleted == (fail_step == 1 ? 0 : 1));
  }
}
