// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_idf_uart.hpp"
#include <cassert>
#include <cstring>
#include <deque>
std::deque<uart_event_t> events;
int installs = 0, reads = 0, flushes = 0;
int flush_result = ESP_OK;
bool inject_fault = false;
unsigned last_wait = 0;
unsigned uxQueueMessagesWaiting(QueueHandle_t)
{
  return events.size();
}
int xQueueReceive(QueueHandle_t, void* out, unsigned wait)
{
  assert(wait == 0);
  if (events.empty())
    return 0;
  *static_cast<uart_event_t*>(out) = events.front();
  events.pop_front();
  return pdTRUE;
}
int gpio_reset_pin(gpio_num_t)
{
  return ESP_OK;
}
int uart_driver_install(int port, int rx, int tx, int depth, QueueHandle_t* out, int flags)
{
  assert(port == UART_NUM_1 && rx == 4096 && tx == 1024 && depth == 32 && flags == 0);
  ++installs;
  *out = &events;
  return ESP_OK;
}
int uart_driver_delete(int)
{
  --installs;
  return ESP_OK;
}
int uart_param_config(int, const uart_config_t*)
{
  return ESP_OK;
}
int uart_set_pin(int, int, int, int, int)
{
  return ESP_OK;
}
int uart_read_bytes(int, void* out, std::size_t capacity, unsigned wait)
{
  ++reads;
  last_wait = wait;
  if (inject_fault)
    events.push_back({UART_FIFO_OVF, 0});
  const char data[] = "<OK\r\n";
  assert(capacity >= sizeof(data));
  std::memcpy(out, data, sizeof(data));
  return sizeof(data);
}
int uart_write_bytes(int, const void*, std::size_t size)
{
  return size;
}
int uart_flush_input(int)
{
  ++flushes;
  return flush_result;
}
int main()
{
  ug::embedded::NativeGnssUart uart;
  ug::embedded::Settings settings;
  settings.gnss_tx_pin = 17;
  settings.gnss_rx_pin = 18;
  const std::uint64_t pins = (UINT64_C(1) << 17) | (UINT64_C(1) << 18);
  assert(!uart.Open(settings, 0) && installs == 0);
  assert(uart.Open(settings, pins) && installs == 1);
  std::uint8_t bytes[512];
  assert(uart.ReadBatch(bytes, sizeof(bytes), true).bytes_read > 0 && last_wait == 1);
  assert(uart.ReadBatch(bytes, sizeof(bytes), false).bytes_read > 0 && last_wait == 0);
  // A driver-reported loss before the read prevents delivery entirely.
  events.push_back({UART_BUFFER_FULL, 0});
  const auto before = reads;
  assert(uart.Read(bytes, sizeof(bytes)).bytes_read == 0 && reads == before);
  assert(uart.TakeFault() && flushes == 1 && uart.overflows == 1);
  // A fault racing the SDK read also suppresses that entire batch.
  inject_fault = true;
  assert(uart.Read(bytes, sizeof(bytes)).bytes_read == 0);
  inject_fault = false;
  assert(uart.TakeFault() && uart.overflows == 2);
  for (const auto type : {UART_PARITY_ERR, UART_FRAME_ERR, UART_BREAK})
    events.push_back({type, 0});
  assert(uart.Read(bytes, sizeof(bytes)).bytes_read == 0);
  assert(uart.TakeFault() && uart.line_errors == 3);
  // A full event queue is ambiguous even when it contains only DATA notices.
  for (int i = 0; i < 32; ++i)
    events.push_back({UART_DATA, 1});
  assert(uart.Read(bytes, sizeof(bytes)).bytes_read == 0 && uart.event_saturations == 1);
  flush_result = ESP_FAIL;
  assert(uart.TakeFault());
  const auto failed_reads = reads;
  assert(uart.Read(bytes, sizeof(bytes)).bytes_read == 0 && reads == failed_reads);
  flush_result = ESP_OK;
  assert(uart.TakeFault());
  assert(uart.Read(bytes, sizeof(bytes)).bytes_read > 0);
  assert(!uart.TakeFault());
  uart.Close();
  assert(installs == 0 && !uart.IsOpen());
}
