/* SPDX-License-Identifier: LGPL-3.0-only */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef CONFIG_IDF_TARGET_ESP32S3
#define CONFIG_IDF_TARGET_ESP32S3 1
#endif
#define CONFIG_ESPNOW_RTCM_BASE 1
#define CONFIG_ESPNOW_RTCM_ROVER 0
#define ESP_ERR_NOT_SUPPORTED 8
#define ESP_ERR_TIMEOUT 9
#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_8BIT 2U
#define MALLOC_CAP_SPIRAM 4U
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void test_lock(void);
void test_unlock(void);
#define portENTER_CRITICAL(x) test_lock()
#define portEXIT_CRITICAL(x) test_unlock()
typedef void* TaskHandle_t;
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task);
int xPortGetCoreID(void);
typedef int* SemaphoreHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
void* heap_caps_malloc(size_t, uint32_t);
int64_t esp_timer_get_time(void);
uint32_t esp_random(void);
void esp_fill_random(void*, size_t);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int, unsigned);
void vTaskDelay(unsigned);
int xTaskCreate(void (*)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t*);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
void xSemaphoreGive(SemaphoreHandle_t);
typedef struct
{
  int unused;
} esp_now_send_info_t;
typedef int esp_now_send_status_t;
#define ESP_NOW_SEND_SUCCESS 0
typedef struct
{
  bool nvs_enable;
  int static_rx_buf_num, dynamic_rx_buf_num, dynamic_tx_buf_num;
  int ampdu_rx_enable, ampdu_tx_enable, rx_ba_win;
  int tx_buf_type, static_tx_buf_num, rx_mgmt_buf_num, espnow_max_encrypt_num;
} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){.nvs_enable = true})
typedef int wifi_mode_t;
typedef int wifi_second_chan_t;
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3
#define WIFI_STORAGE_RAM 0
#define WIFI_PS_NONE 0
#define WIFI_BW20 0
#define WIFI_IF_STA 0
#define WIFI_SECOND_CHAN_NONE 0
typedef struct
{
  uint8_t channel;
  int ifidx;
  bool encrypt;
  uint8_t lmk[16], peer_addr[6];
} esp_now_peer_info_t;
esp_err_t esp_wifi_get_mode(wifi_mode_t*);
esp_err_t esp_wifi_init(const wifi_init_config_t*);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_ps(int);
esp_err_t esp_wifi_set_bandwidth(int, int);
esp_err_t esp_wifi_set_channel(uint8_t, int);
esp_err_t esp_wifi_get_channel(uint8_t*, wifi_second_chan_t*);
esp_err_t esp_wifi_get_mac(int, uint8_t*);
esp_err_t esp_now_init(void);
esp_err_t esp_now_set_pmk(const uint8_t*);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t*);
esp_err_t esp_now_register_send_cb(void (*)(const esp_now_send_info_t*, esp_now_send_status_t));
esp_err_t esp_now_send(const uint8_t*, const uint8_t*, size_t);
