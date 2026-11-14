// SPDX-License-Identifier: LGPL-3.0-only
#include "driver/usb_serial_jtag.h"
#include "esp_mac.h"
#include "nvs.h"
#include "ug_pairing.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
bool ug_rover_configure(bool, ug_pairing_t*);
static char input[1024], output[4096];
static size_t offset, used, writes;
static int64_t clock_us;
static bool present, fail_commit, fail_drain;
void vTaskDelay(unsigned ticks)
{
  clock_us += (int64_t)ticks * 1000;
}
static uint8_t saved[UG_PAIRING_RECORD_SIZE];
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
  assert(h == 1 && !strcmp(name, "pairing"));
  if (!present)
    return ESP_ERR_NVS_NOT_FOUND;
  assert(*n >= sizeof(saved));
  memcpy(p, saved, sizeof(saved));
  *n = sizeof(saved);
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char* name, const void* p, size_t n)
{
  assert(h == 1 && !strcmp(name, "pairing") && n == sizeof(saved));
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
