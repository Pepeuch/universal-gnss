// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_log.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include <stdbool.h>

#ifndef UG_S3_RADIO_PROFILE
#define UG_S3_RADIO_PROFILE 1
#endif
#if UG_S3_RADIO_PROFILE < 0 || UG_S3_RADIO_PROFILE > 4
#error "UG_S3_RADIO_PROFILE must be between 0 and 4"
#endif

// Called before the first esp_wifi_init. STA/AP services retain their SDK defaults.
static inline void ug_apply_s3_radio_policy(wifi_init_config_t* config, bool radio_owns_wifi)
{
#if CONFIG_IDF_TARGET_ESP32S3 && CONFIG_ESPNOW_RTCM_BASE
  const int profile = radio_owns_wifi ? UG_S3_RADIO_PROFILE : 0;
  if (profile != 0)
  {
    config->static_rx_buf_num = profile == 3 ? 4 : 6;
    if (profile >= 2)
    {
      config->dynamic_rx_buf_num = 16;
      config->dynamic_tx_buf_num = 16;
    }
    if (profile >= 3)
    {
      config->ampdu_rx_enable = 0;
      config->ampdu_tx_enable = 0;
      config->rx_ba_win = 0;
    }
  }
  ESP_LOGI("UG_WIFI",
           "init profile=%d radio_owner=%d rx_static=%d rx_dynamic=%d tx_type=%d tx_dynamic=%d "
           "ampdu_rx=%d ampdu_tx=%d rx_ba=%d",
           profile,
           (int)radio_owns_wifi,
           config->static_rx_buf_num,
           config->dynamic_rx_buf_num,
           config->tx_buf_type,
           config->dynamic_tx_buf_num,
           config->ampdu_rx_enable,
           config->ampdu_tx_enable,
           config->rx_ba_win);
#else
  (void)config;
  (void)radio_owns_wifi;
#endif
}
