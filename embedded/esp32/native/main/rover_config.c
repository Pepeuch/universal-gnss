// SPDX-License-Identifier: LGPL-3.0-only
// Maintenance runs before Wi-Fi/radio/UART output. It never shares a live RTCM stream.
#include "ug_pairing.h"
#include "ug_portable.h"

// IDF 6.1 USB header requires bool to be declared first in C17.
#include "driver/usb_serial_jtag.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#ifndef UG_ROVER_UART_TX
#define UG_ROVER_UART_TX -1
#define UG_ROVER_UART_BAUD 115200
#endif
static ug_portable_t portable;
const ug_portable_t* ug_rover_portable_current(void)
{
  return &portable;
}
static bool portable_save(nvs_handle_t h, const ug_portable_t* value)
{
  uint8_t data[UG_PORTABLE_RECORD_SIZE], check[UG_PORTABLE_RECORD_SIZE];
  size_t n = sizeof(check);
  return ug_portable_record(value, data) &&
         nvs_set_blob(h, "portable", data, sizeof(data)) == ESP_OK && nvs_commit(h) == ESP_OK &&
         nvs_get_blob(h, "portable", check, &n) == ESP_OK && n == sizeof(data) &&
         !memcmp(data, check, sizeof(data));
}
static int portable_load(nvs_handle_t h)
{
  uint8_t data[UG_PORTABLE_RECORD_SIZE];
  size_t n = sizeof(data);
  const esp_err_t e = nvs_get_blob(h, "portable", data, &n);
  if (e == ESP_ERR_NVS_NOT_FOUND)
    return 0;
  if (e != ESP_OK)
    return 2;
  if (n > 4 && !memcmp(data, "UGPC", 4) && data[4] != 1)
    return 2;
  ug_portable_t value;
  if (!ug_portable_restore(data, n, &value) || value.board != UG_BOARD_C3)
    return 1;
  portable = value;
  return 0;
}
static bool reply(const char* text)
{
  const size_t n = strlen(text);
  return usb_serial_jtag_write_bytes(text, n, pdMS_TO_TICKS(200)) == (int)n &&
         usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000)) == ESP_OK;
}
static bool portable_reply(const char* state)
{
  char json[UG_PORTABLE_JSON_MAX + 1], response[UG_PORTABLE_JSON_MAX + 96];
  if (!ug_portable_json(&portable, json, sizeof(json)))
    return reply("UGCFG/1 {\"error\":\"portable_invalid\"}\n");
  snprintf(
      response, sizeof(response), "UGCFG/1 {\"portable\":%s,\"result\":\"%s\"}\n", json, state);
  return reply(response);
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
  portable = ug_portable_default(UG_BOARD_C3);
  if (UG_ROVER_UART_TX >= 0)
  {
    portable.output = UG_OUTPUT_UART;
    portable.tx = UG_ROVER_UART_TX;
    portable.baud = UG_ROVER_UART_BAUD;
  }
  const int portable_state = writable ? portable_load(handle) : 2;
  bool portable_ok = portable_state == 0;
  if (portable_state == 2)
    writable = false;
  uint8_t preferred_channel = portable.channel;
  if (pairing.paired)
    portable.channel = pairing.channel;
  unsigned polled = 0;
  bool opened = false, overflow = false;
  size_t size = 0;
  char line[UG_PORTABLE_JSON_MAX + 64], nonce[17] = {0};
  int64_t deadline = esp_timer_get_time() + 8000000;
  for (;;)
  {
    const int64_t now = esp_timer_get_time();
    if (now >= deadline)
    {
      if (opened && !reply((writable && portable_ok && pairing.paired)
                               ? "UGCFG/1 {\"mode\":\"closed\"}\n"
                               : "UGCFG/1 {\"error\":\"maintenance_expired_not_running\"}\n"))
        break;
      if (writable && portable_ok && pairing.paired)
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
      if (size < sizeof(line) - 1 && !overflow)
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
      line[size] = 0;
      const char* check = "UGCFG/1 CONFIG_CHECK ";
      const char* apply = "UGCFG/1 CONFIG_APPLY CONFIRM ";
      const bool is_check =
          !overflow && size > strlen(check) && !memcmp(line, check, strlen(check));
      const bool is_apply =
          !overflow && size > strlen(apply) && !memcmp(line, apply, strlen(apply));
      if (!overflow && !strcmp(line, "UGCFG/1 CONFIG_GET"))
      {
        if (!portable_ok)
        {
          if (!reply("UGCFG/1 {\"error\":\"portable_invalid\"}\n"))
            break;
        }
        else if (!portable_reply("current"))
          break;
        size = 0;
        memset(line, 0, sizeof(line));
        continue;
      }
      if (is_check || is_apply)
      {
        const size_t prefix = strlen(is_check ? check : apply);
        ug_portable_t candidate;
        if (!ug_portable_parse(line + prefix, size - prefix, &candidate) ||
            candidate.board != UG_BOARD_C3 ||
            (pairing.paired && candidate.channel != pairing.channel))
        {
          if (!reply("UGCFG/1 {\"error\":\"portable_incompatible\"}\n"))
            break;
        }
        else if (is_apply && (!writable || !portable_save(handle, &candidate)))
        {
          writable = false;
          if (!reply("UGCFG/1 {\"error\":\"storage_verification_failed\"}\n"))
            break;
        }
        else
        {
          const ug_portable_t before = portable;
          portable = candidate;
          const bool sent = portable_reply(is_apply ? "saved" : "valid");
          if (is_apply)
          {
            portable_ok = true;
            preferred_channel = portable.channel;
          }
          else
            portable = before;
          if (!sent)
            break;
        }
        size = 0;
        memset(line, 0, sizeof(line));
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
        if (!writable || !portable_ok || !pairing.paired)
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
          portable.channel = pairing.paired ? pairing.channel : preferred_channel;
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
