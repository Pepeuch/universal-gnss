// SPDX-License-Identifier: LGPL-3.0-only
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "espnow_rtcm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ug_pairing.h"
#include "ug_portable.h"
#include <stdio.h>
bool ug_rover_configure(bool nvs_ready, ug_pairing_t* result);
const ug_portable_t* ug_rover_portable_current(void);
esp_err_t ug_rover_uart_start(int tx, uint32_t baud);

void app_main(void)
{
  esp_log_level_set("*", ESP_LOG_NONE);
  // Never erase persistent configuration automatically, including on version errors.
  ug_pairing_t pairing;
  if (!ug_rover_configure(nvs_flash_init() == ESP_OK, &pairing))
    return;
  char peer[18], key[33];
  snprintf(peer,
           sizeof(peer),
           "%02x:%02x:%02x:%02x:%02x:%02x",
           pairing.peer[0],
           pairing.peer[1],
           pairing.peer[2],
           pairing.peer[3],
           pairing.peer[4],
           pairing.peer[5]);
  for (size_t i = 0; i < 16; ++i)
    snprintf(key + i * 2, 3, "%02x", pairing.key[i]);
  if (espnow_rtcm_configure_secure(peer, key, pairing.channel) != ESP_OK)
    return;
  const ug_portable_t* output = ug_rover_portable_current();
  if (output->output == UG_OUTPUT_UART && ug_rover_uart_start(output->tx, output->baud) != ESP_OK)
    return;
  if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK ||
      espnow_rtcm_start() != ESP_OK)
    return;
  for (;;)
    vTaskDelay(pdMS_TO_TICKS(1000));
}
