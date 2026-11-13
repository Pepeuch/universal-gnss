// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
#include "freertos/queue.h"
#include <cstddef>
#include <cstdint>
constexpr int UART_NUM_1 = 1, UART_DATA_8_BITS = 8, UART_PARITY_DISABLE = 0, UART_STOP_BITS_1 = 1,
              UART_HW_FLOWCTRL_DISABLE = 0, UART_SCLK_DEFAULT = 0, UART_PIN_NO_CHANGE = -1;
enum uart_event_type_t
{
  UART_DATA,
  UART_FIFO_OVF,
  UART_BUFFER_FULL,
  UART_PARITY_ERR,
  UART_FRAME_ERR,
  UART_BREAK
};
struct uart_event_t
{
  uart_event_type_t type;
  std::size_t size;
};
struct uart_config_t
{
  int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
};
int uart_driver_delete(int);
int uart_driver_install(int, int, int, int, QueueHandle_t*, int);
int uart_param_config(int, const uart_config_t*);
int uart_set_pin(int, int, int, int, int);
int uart_read_bytes(int, void*, std::size_t, unsigned);
int uart_write_bytes(int, const void*, std::size_t);
int uart_flush_input(int);
