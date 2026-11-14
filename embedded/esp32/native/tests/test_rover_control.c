// SPDX-License-Identifier: LGPL-3.0-only
#include "driver/usb_serial_jtag.h"
#include "esp_mac.h"
#include "nvs.h"
#include "ug_pairing.h"
#include "ug_portable.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
bool ug_rover_configure(bool, ug_pairing_t*);
const ug_portable_t* ug_rover_portable_current(void);
static char input[1024], output[4096];
static size_t offset, used, writes;
static int64_t clock_us;
static bool present, fail_commit, fail_drain;
void vTaskDelay(unsigned ticks)
{
  clock_us += (int64_t)ticks * 1000;
}
static uint8_t saved[UG_PAIRING_RECORD_SIZE], portable_saved[UG_PORTABLE_RECORD_SIZE];
static bool portable_present;
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t* p)
{
  assert(p->rx_buffer_size >= 128);
  return ESP_OK;
}
int64_t esp_timer_get_time(void)
{
  return clock_us;
}
int usb_serial_jtag_read_bytes(void* p, size_t n, uint32_t wait)
{
  assert(n == 1 && wait <= 20);
  clock_us += 20000;
  assert(clock_us < 130000000);
  if (offset >= strlen(input))
    return 0;
  *(char*)p = input[offset++];
  return 1;
}
int usb_serial_jtag_write_bytes(const void* p, size_t n, uint32_t wait)
{
  assert(wait <= 200 && used + n < sizeof(output));
  memcpy(output + used, p, n);
  used += n;
  output[used] = 0;
  // Stop simulated locked sessions after the error, without an infinite USB loop.
  if (strstr(output, "storage_") || strstr(output, "unpaired_or_locked"))
    return 0;
  return (int)n;
}
esp_err_t usb_serial_jtag_wait_tx_done(uint32_t wait)
{
  assert(wait <= 1000);
  return fail_drain ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_read_mac(uint8_t* mac, int kind)
{
  assert(kind == ESP_MAC_WIFI_STA);
  const uint8_t id[] = {2, 0, 0, 0, 0, 3};
  memcpy(mac, id, 6);
  return ESP_OK;
}
esp_err_t nvs_open(const char* name, int mode, nvs_handle_t* h)
{
  assert(!strcmp(name, "ug_rover") && mode == NVS_READWRITE);
  *h = 1;
  return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char* name, void* p, size_t* n)
{
  assert(h == 1);
  if (!strcmp(name, "portable"))
  {
    if (!portable_present)
      return ESP_ERR_NVS_NOT_FOUND;
    assert(*n >= sizeof(portable_saved));
    memcpy(p, portable_saved, sizeof(portable_saved));
    *n = sizeof(portable_saved);
    return ESP_OK;
  }
  assert(!strcmp(name, "pairing"));
  if (!present)
    return ESP_ERR_NVS_NOT_FOUND;
  assert(*n >= sizeof(saved));
  memcpy(p, saved, sizeof(saved));
  *n = sizeof(saved);
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char* name, const void* p, size_t n)
{
  assert(h == 1);
  if (!strcmp(name, "portable"))
  {
    assert(n == sizeof(portable_saved));
    memcpy(portable_saved, p, n);
    portable_present = true;
    ++writes;
    return ESP_OK;
  }
  assert(!strcmp(name, "pairing") && n == sizeof(saved));
  memcpy(saved, p, n);
  present = true;
  ++writes;
  return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
  assert(h == 1);
  return fail_commit ? ESP_FAIL : ESP_OK;
}
void nvs_close(nvs_handle_t h)
{
  assert(h == 1);
}
static void script(const char* text)
{
  strcpy(input, text);
  used = offset = clock_us = 0;
  output[0] = 0;
}
int main(void)
{
  const char* key = "0123456789abcdef0123456789abcdef";
  ug_pairing_t p;
  script("UGCFG/1 OPEN 0123456789abcdef\nUGCFG/1 GET\nUGCFG/1 SET 02:00:00:00:00:02 1 "
         "0123456789abcdef0123456789abcdef CONFIRM_PAIR\nUGCFG/1 RUN CONFIRM_RTCM\n");
  assert(ug_rover_configure(true, &p) && p.paired && p.channel == 1 && writes == 1);
  assert(!strstr(output, key) && strstr(output, "\"mode\":\"control\"") &&
         strstr(output, "\"mode\":\"closed\""));
  assert(!memchr(output, 0xd3, used));
  // A normal paired reboot emits zero control bytes, even if given ordinary input.
  script("noise\nUGCFG/1 GET\n");
  assert(ug_rover_configure(true, &p) && p.paired && !used && writes == 1);
  script("UGCFG/1 OPEN 0123456789abcdef\nUGCFG/1 GET\n");
  fail_drain = true;
  assert(!ug_rover_configure(true, &p));
  fail_drain = false;
  char config[513], commands[1024];
  ug_portable_t output_config = ug_portable_default(UG_BOARD_C3);
  output_config.output = UG_OUTPUT_UART;
  output_config.tx = 4;
  output_config.baud = 230400;
  assert(ug_portable_json(&output_config, config, sizeof(config)));
  snprintf(commands,
           sizeof(commands),
           "UGCFG/1 OPEN 0123456789abcdef\nUGCFG/1 CONFIG_CHECK %s\nUGCFG/1 CONFIG_APPLY CONFIRM "
           "%s\nUGCFG/1 CONFIG_GET\nUGCFG/1 RUN CONFIRM_RTCM\n",
           config,
           config);
  script(commands);
  assert(ug_rover_configure(true, &p));
  assert(writes == 2 && portable_present);
  assert(ug_rover_portable_current()->output == UG_OUTPUT_UART &&
         ug_rover_portable_current()->tx == 4);
  assert(strstr(output, "\"result\":\"valid\"") && strstr(output, "\"result\":\"saved\""));
  script("");
  assert(ug_rover_configure(true, &p) && !used && writes == 2);
  assert(ug_rover_portable_current()->baud == 230400);
  // Invalid portable content can be replaced only by an explicitly confirmed valid record.
  portable_saved[0] ^= 1;
  snprintf(
      commands,
      sizeof(commands),
      "UGCFG/1 OPEN 0123456789abcdef\nUGCFG/1 CONFIG_APPLY CONFIRM %s\nUGCFG/1 RUN CONFIRM_RTCM\n",
      config);
  script(commands);
  assert(ug_rover_configure(true, &p) && writes == 3);
  // Rejected pin/channel requests retain the prior output and private pairing.
  snprintf(
      commands,
      sizeof(commands),
      "UGCFG/1 OPEN 0123456789abcdef\nUGCFG/1 CONFIG_APPLY CONFIRM %s\nUGCFG/1 RUN CONFIRM_RTCM\n",
      config);
  char* pin = strstr(commands, "\"uart_tx\":4");
  assert(pin);
  pin[strlen("\"uart_tx\":")] = '8';
  script(commands);
  assert(ug_rover_configure(true, &p) && writes == 3 && strstr(output, "portable_incompatible"));
  assert(ug_rover_portable_current()->tx == 4);
  // Clearing a private association restores the separately persisted channel preference.
  output_config.channel = 2;
  assert(ug_portable_json(&output_config, config, sizeof(config)));
  snprintf(commands,
           sizeof(commands),
           "UGCFG/1 OPEN 0123456789abcdef\nUGCFG/1 SET 02:00:00:00:00:02 2 "
           "0123456789abcdef0123456789abcdef CONFIRM_PAIR\nUGCFG/1 CONFIG_APPLY CONFIRM "
           "%s\nUGCFG/1 CLEAR CONFIRM_UNPAIR\nUGCFG/1 CONFIG_GET\nUGCFG/1 SET 02:00:00:00:00:02 1 "
           "0123456789abcdef0123456789abcdef CONFIRM_PAIR\nUGCFG/1 RUN CONFIRM_RTCM\n",
           config);
  script(commands);
  assert(ug_rover_configure(true, &p));
  assert(strstr(output, "\"radio_channel\":2},\"result\":\"current\""));
  writes = 3;
  output_config.channel = 1;
  assert(ug_portable_json(&output_config, config, sizeof(config)));
  // A future schema is preserved and cannot be overwritten by this firmware.
  portable_saved[4] = 2;
  snprintf(commands,
           sizeof(commands),
           "UGCFG/1 OPEN 0123456789abcdef\nUGCFG/1 CONFIG_APPLY CONFIRM %s\n",
           config);
  script(commands);
  assert(!ug_rover_configure(true, &p) && writes == 3 && portable_saved[4] == 2);
  portable_present = false;
  writes = 1;
  // Invalid persisted records lock writes and refuse the data plane; no erase.
  saved[0] ^= 1;
  script("UGCFG/1 OPEN 0123456789abcdef\nUGCFG/1 SET 02:00:00:00:00:02 1 "
         "0123456789abcdef0123456789abcdef CONFIRM_PAIR\n");
  assert(!ug_rover_configure(true, &p) && writes == 1 && strstr(output, "storage_locked"));
  present = false;
  fail_commit = true;
  script("UGCFG/1 OPEN 0123456789abcdef\nUGCFG/1 SET 02:00:00:00:00:02 1 "
         "0123456789abcdef0123456789abcdef CONFIRM_PAIR\n");
  assert(!ug_rover_configure(true, &p) && strstr(output, "storage_verification_failed"));
}
