// SPDX-License-Identifier: LGPL-3.0-only
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "espnow_rtcm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

void app_main(void)
{
  esp_log_level_set("*", ESP_LOG_NONE);
  // Never erase persistent configuration automatically, including on version errors.
  if (nvs_flash_init() != ESP_OK || esp_netif_init() != ESP_OK ||
      esp_event_loop_create_default() != ESP_OK || espnow_rtcm_start() != ESP_OK)
    return;
  for (;;)
    vTaskDelay(pdMS_TO_TICKS(1000));
}
