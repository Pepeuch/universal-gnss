/* SPDX-License-Identifier: LGPL-3.0-only */
#include "esp_radio_test_sdk.h"
#include "ug_rtcm.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
/* Compile the actual driver, including its private worker/ACK path. No radio. */
#include "../../components/espnow_rtcm/src/espnow_rtcm.c"
static unsigned allocations, sdk_calls, sends, locks;
static bool fail_alloc, fail_ack, fail_wifi, fail_callback;
static int semaphore;
static uint32_t clock_ms = 100;
static jmp_buf stop_worker;
static unsigned turns;
void test_lock(void)
{
  ++locks;
}
void test_unlock(void)
{
  assert(locks);
  --locks;
}
void* heap_caps_malloc(size_t size, uint32_t caps)
{
  assert(!locks && caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  assert(size == sizeof(espnow_rtcm_storage_data_t));
  ++allocations;
  return fail_alloc ? NULL : malloc(size);
}
int64_t esp_timer_get_time(void)
{
  return (int64_t)clock_ms * 1000;
}
uint32_t esp_random(void)
{
  return 17;
}
void esp_fill_random(void* p, size_t n)
{
  memset(p, 0x5a, n);
}
void xTaskNotifyGive(TaskHandle_t t)
{
  assert(t);
}
unsigned ulTaskNotifyTake(int clear, unsigned wait)
{
  (void)clear;
  (void)wait;
  if (turns++)
    longjmp(stop_worker, 1);
  return 1;
}
void vTaskDelay(unsigned ticks)
{
  clock_ms += ticks;
}
int xTaskCreate(void (*fn)(void*),
                const char* name,
                unsigned stack,
                void* arg,
                unsigned priority,
                TaskHandle_t* t)
{
  assert(fn == worker && stack == 4096 && priority == 4);
  (void)name;
  (void)arg;
  *t = (void*)1;
  return pdPASS;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
  semaphore = 0;
  return &semaphore;
}
int xSemaphoreTake(SemaphoreHandle_t s, unsigned ms)
{
  assert(s == &semaphore && ms == 200);
  int n = *s;
  *s = 0;
  return n;
}
void xSemaphoreGive(SemaphoreHandle_t s)
{
  assert(s == &semaphore);
  *s = 1;
}
esp_err_t esp_wifi_get_mode(wifi_mode_t* mode)
{
  ++sdk_calls;
  *mode = WIFI_MODE_STA;
  return ESP_OK;
}
esp_err_t esp_wifi_init(const wifi_init_config_t* cfg)
{
  assert(!cfg->nvs_enable);
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_wifi_set_storage(int n)
{
  (void)n;
  ++sdk_calls;
  return fail_wifi ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_wifi_set_mode(int n)
{
  (void)n;
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_wifi_start(void)
{
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_wifi_set_ps(int n)
{
  (void)n;
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_wifi_set_bandwidth(int a, int b)
{
  (void)a;
  (void)b;
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_wifi_set_channel(uint8_t a, int b)
{
  assert(a == 1);
  (void)b;
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_wifi_get_channel(uint8_t* a, wifi_second_chan_t* b)
{
  *a = 1;
  *b = WIFI_SECOND_CHAN_NONE;
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_wifi_get_mac(int n, uint8_t* p)
{
  (void)n;
  memset(p, 2, 6);
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_now_init(void)
{
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_now_set_pmk(const uint8_t* p)
{
  assert(p);
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_now_add_peer(const esp_now_peer_info_t* p)
{
  assert(p->encrypt && p->channel == 1);
  ++sdk_calls;
  return ESP_OK;
}
esp_err_t esp_now_register_send_cb(void (*cb)(const esp_now_send_info_t*, esp_now_send_status_t))
{
  assert(cb == sent);
  ++sdk_calls;
  return fail_callback ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_now_send(const uint8_t* peer, const uint8_t* bytes, size_t n)
{
  assert(peer && bytes && n <= ESPNOW_RTCM_PACKET_SIZE);
  ++sends;
  if (!fail_ack)
    sent(NULL, ESP_NOW_SEND_SUCCESS);
  return ESP_OK;
}
static void step(void)
{
  turns = 0;
  if (!setjmp(stop_worker))
    worker(NULL);
}
static void frame(uint8_t* bytes)
{
  uint32_t crc;
  bytes[0] = 0xd3;
  bytes[1] = 0;
  bytes[2] = 2;
  bytes[3] = 0x3e;
  bytes[4] = 0x80;
  assert(ug_rtcm_crc24q(bytes, 5, &crc));
  bytes[5] = crc >> 16;
  bytes[6] = crc >> 8;
  bytes[7] = crc;
}
int main(int argc, char** argv)
{
  assert(argc == 2);
  espnow_rtcm_stats_t stats;
  espnow_rtcm_get_stats(&stats);
  assert(!stats.started && !stats.queued && !allocations);
  espnow_rtcm_invalidate_pending();
  espnow_rtcm_ntrip_reset();
  espnow_rtcm_ntrip_response(false);
  assert(!allocations && !sdk_calls);
  assert(espnow_rtcm_start() == ESP_ERR_INVALID_STATE && !allocations);
  assert(espnow_rtcm_configure_secure("02:00:00:00:00:02", "000102030405060708090a0b0c0d0e0f", 1) ==
         ESP_OK);
  if (!strcmp(argv[1], "oom"))
    fail_alloc = true;
  if (!strcmp(argv[1], "wifi-failure"))
    fail_wifi = true;
  if (!strcmp(argv[1], "callback-failure"))
    fail_callback = true;
  if (!strcmp(argv[1], "stop-before-start"))
    espnow_rtcm_stop_until_reboot();
  const int result = espnow_rtcm_start();
  assert(allocations == 1);
  espnow_rtcm_storage_data_t* saved = espnow_rtcm_storage_get(&s_storage);
  if (fail_alloc)
  {
    assert(result == ESP_ERR_NO_MEM && !saved && !sdk_calls);
  }
  else if (fail_wifi || fail_callback)
  {
    assert(result == ESP_FAIL && saved && !s_started);
  }
  else
  {
    assert(result == ESP_OK && saved && s_started);
    uint8_t bytes[8];
    frame(bytes);
    assert(espnow_rtcm_send_frame_at(bytes, 8, clock_ms - 1001) == ESP_ERR_INVALID_ARG);
    for (unsigned i = 0; i < ESPNOW_RTCM_QUEUE_DEPTH; ++i)
      assert(espnow_rtcm_send_frame_at(bytes, 8, clock_ms) == ESP_OK);
    assert(espnow_rtcm_send_frame_at(bytes, 8, clock_ms) == ESP_ERR_NO_MEM);
    espnow_rtcm_invalidate_pending();
    assert(s_queue.count == 0);
    assert(espnow_rtcm_send_frame_at(bytes, 8, clock_ms) == ESP_OK);
    if (!strcmp(argv[1], "late-ack"))
      fail_ack = true;
    if (!strcmp(argv[1], "expired"))
      clock_ms += 1001;
    if (!strcmp(argv[1], "epoch-overflow"))
    {
      s_tx_epoch = UINT32_MAX;
      espnow_rtcm_invalidate_pending();
      assert(s_tx_halted);
    }
    step();
    if (fail_ack)
    {
      assert(s_tx_halted && sends == 1);
      sent(NULL, ESP_NOW_SEND_SUCCESS);
      assert(espnow_rtcm_send_frame_at(bytes, 8, clock_ms) == ESP_OK);
      step();
      assert(sends == 1 && s_tx_halted);
    }
    else if (!strcmp(argv[1], "expired") || !strcmp(argv[1], "epoch-overflow"))
      assert(sends == 0);
    else if (!strcmp(argv[1], "stop-before-start"))
      assert(sends == 0 && s_tx_halted);
    else
      assert(sends == 1);
    unsigned before = sends;
    espnow_rtcm_stop_until_reboot();
    espnow_rtcm_invalidate_pending();
    assert(espnow_rtcm_send_frame_at(bytes, 8, clock_ms) == ESP_OK);
    step();
    assert(sends == before && s_tx_halted);
  }
  fail_alloc = false;
  assert(espnow_rtcm_start() == ESP_ERR_INVALID_STATE && allocations == 1);
  assert(espnow_rtcm_configure_secure("02:00:00:00:00:04", "000102030405060708090a0b0c0d0e0f", 1) ==
         ESP_ERR_INVALID_STATE);
  espnow_rtcm_get_stats(&stats);
  espnow_rtcm_invalidate_pending();
  espnow_rtcm_stop_until_reboot();
  assert(espnow_rtcm_storage_get(&s_storage) == saved && !locks);
  /* Simulated reboot only, after every fake task/callback has finished. Runtime never frees. */
  free(saved);
  return 0;
}
