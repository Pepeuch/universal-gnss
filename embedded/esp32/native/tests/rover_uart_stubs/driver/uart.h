// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
enum
{
  UART_NUM_1 = 1,
  UART_DATA_8_BITS = 8,
  UART_PARITY_DISABLE = 0,
  UART_STOP_BITS_1 = 1,
  UART_HW_FLOWCTRL_DISABLE = 0,
  UART_SCLK_DEFAULT = 0,
  UART_PIN_NO_CHANGE = -1
};
typedef struct
{
  int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
} uart_config_t;
esp_err_t uart_driver_install(int, int, int, int, void*, int);
esp_err_t uart_driver_delete(int);
esp_err_t uart_param_config(int, const uart_config_t*);
esp_err_t uart_set_pin(int, int, int, int, int);
esp_err_t uart_disable_rx_intr(int);
int uart_tx_chars(int, const char*, uint32_t);
