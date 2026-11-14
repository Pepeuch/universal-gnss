// SPDX-License-Identifier: LGPL-3.0-only
// Maintenance runs before Wi-Fi/radio/UART output. It never shares a live RTCM stream.
#include "ug_pairing.h"

// IDF 6.1 USB header requires bool to be declared first in C17.
#include "driver/usb_serial_jtag.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
static bool reply(const char* text)
{
  const size_t n = strlen(text);
  return usb_serial_jtag_write_bytes(text, n, pdMS_TO_TICKS(200)) == (int)n &&
         usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000)) == ESP_OK;
}
static bool load(nvs_handle_t handle, ug_pairing_t* pairing)
{
  uint8_t record[UG_PAIRING_RECORD_SIZE];
  size_t size = sizeof(record);
  const esp_err_t err = nvs_get_blob(handle, "pairing", record, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND)
  {
    *pairing = (ug_pairing_t){.channel = 1};
    return true;
  }
  return err == ESP_OK && ug_pairing_decode(record, size, pairing);
}
static bool save(nvs_handle_t handle, const ug_pairing_t* pairing)
{
  uint8_t record[UG_PAIRING_RECORD_SIZE], check[UG_PAIRING_RECORD_SIZE];
  size_t size = sizeof(check);
  return ug_pairing_encode(pairing, record) &&
         nvs_set_blob(handle, "pairing", record, sizeof(record)) == ESP_OK &&
         nvs_commit(handle) == ESP_OK && nvs_get_blob(handle, "pairing", check, &size) == ESP_OK &&
         size == sizeof(record) && !memcmp(record, check, sizeof(record));
}
static bool status(const ug_pairing_t* p, bool writable, const uint8_t mac[6], const char* nonce)
{
  char out[320];
  snprintf(out,
           sizeof(out),
           "UGCFG/1 "
           "{\"mode\":\"control\",\"ttl_ms\":120000,\"nonce\":\"%s\",\"writable\":%s,\"paired\":%s,"
           "\"channel\":%u,\"key_set\":%s,\"sta_mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"peer\":"
           "\"%02x:%02x:%02x:%02x:%02x:%02x\"}\n",
           nonce,
           writable ? "true" : "false",
           p->paired ? "true" : "false",
           p->channel,
           p->paired ? "true" : "false",
           mac[0],
           mac[1],
           mac[2],
           mac[3],
           mac[4],
           mac[5],
           p->peer[0],
           p->peer[1],
           p->peer[2],
           p->peer[3],
           p->peer[4],
           p->peer[5]);
  return reply(out);
}
/* Returns only after control TX is drained. False never starts a data plane. */
bool ug_rover_configure(bool nvs_ready, ug_pairing_t* result)
{
  usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 2048, .rx_buffer_size = 512};
  if (usb_serial_jtag_driver_install(&usb) != ESP_OK)
    return false;
  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    return false;
  nvs_handle_t handle = 0;
  bool writable = nvs_ready && nvs_open("ug_rover", NVS_READWRITE, &handle) == ESP_OK;
  ug_pairing_t pairing = {.channel = 1};
  if (writable && !load(handle, &pairing))
    writable = false;
  unsigned polled = 0;
  bool opened = false, overflow = false;
  size_t size = 0;
  char line[UG_PAIRING_LINE_MAX + 1], nonce[17] = {0};
  int64_t deadline = esp_timer_get_time() + 8000000;
  for (;;)
  {
    const int64_t now = esp_timer_get_time();
    if (now >= deadline)
    {
      if (opened && !reply("UGCFG/1 {\"mode\":\"closed\"}\n"))
        break;
      if (writable && pairing.paired)
      {
        *result = pairing;
        if (handle)
          nvs_close(handle);
        return true;
      }
      opened = false;
      deadline = now + 8000000;
      size = 0;
      overflow = false;
    }
    char byte;
    if (usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(20)) != 1)
      continue;
    if ((++polled & 31u) == 0)
      vTaskDelay(1);
    if (byte != '\n')
    {
      if (size < UG_PAIRING_LINE_MAX && !overflow)
        line[size++] = byte;
      else
        overflow = true;
      continue;
    }
    if (!opened)
    {
      if (!overflow && ug_pairing_open(line, size, nonce))
      {
        opened = true;
        deadline = esp_timer_get_time() + 120000000;
        if (!status(&pairing, writable, mac, nonce))
          break;
      }
    }
    else
    {
      if (!overflow && ug_pairing_open(line, size, nonce))
      {
        deadline = esp_timer_get_time() + 120000000;
        if (!status(&pairing, writable, mac, nonce))
          break;
        memset(line, 0, sizeof(line));
        size = 0;
        overflow = false;
        continue;
      }
      ug_pair_command_t command;
      if (overflow || !ug_pairing_command(line, size, &command))
      {
        if (!reply("UGCFG/1 {\"error\":\"invalid_command\"}\n"))
          break;
      }
      else if (command.kind == UG_PAIR_GET)
      {
        if (!status(&pairing, writable, mac, nonce))
          break;
      }
      else if (command.kind == UG_PAIR_RUN)
      {
        if (!writable || !pairing.paired)
        {
          if (!reply("UGCFG/1 {\"error\":\"unpaired_or_locked\"}\n"))
            break;
        }
        else
        {
          if (!reply("UGCFG/1 {\"mode\":\"closed\"}\n"))
            break;
          *result = pairing;
          if (handle)
            nvs_close(handle);
          return true;
        }
      }
      else if (!writable)
      {
        if (!reply("UGCFG/1 {\"error\":\"storage_locked\"}\n"))
          break;
      }
      else
      {
        const ug_pairing_t next =
            command.kind == UG_PAIR_CLEAR ? (ug_pairing_t){.channel = 1} : command.pairing;
        if (!save(handle, &next))
        {
          writable = false;
          if (!reply("UGCFG/1 {\"error\":\"storage_verification_failed\"}\n"))
            break;
        }
        else
        {
          pairing = next;
          if (!status(&pairing, writable, mac, nonce))
            break;
        }
      }
    }
    memset(line, 0, sizeof(line));
    size = 0;
    overflow = false;
  }
  if (handle)
    nvs_close(handle);
  return false;
}
