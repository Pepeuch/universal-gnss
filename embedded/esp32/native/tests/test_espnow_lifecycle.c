/* SPDX-License-Identifier: LGPL-3.0-only */
#include "esp_radio_test_sdk.h"
#include "ug_rtcm.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
void ug_test_pmk_failure(bool fail);
/* Compile the actual driver, including its private worker/ACK path. No radio. */
#include "../../components/espnow_rtcm/src/espnow_rtcm.c"
static unsigned allocations, sdk_calls, sends, locks, lock_entries;
static uintptr_t payload_begin;
static uint8_t expected_packets[ESPNOW_RTCM_QUEUE_DEPTH][ESPNOW_RTCM_PACKET_SIZE];
static size_t expected_lengths[ESPNOW_RTCM_QUEUE_DEPTH], expected_count;
static esp_err_t save_fragment(const lora_transport_fragment_t* fragment, void* ctx)
{
  (void)ctx;
  assert(expected_count < ESPNOW_RTCM_QUEUE_DEPTH &&
         fragment->packet_len <= ESPNOW_RTCM_PACKET_SIZE);
  memcpy(expected_packets[expected_count], fragment->packet, fragment->packet_len);
  expected_lengths[expected_count++] = fragment->packet_len;
  return ESP_OK;
}
static bool fail_alloc, fail_ack, fail_wifi, fail_callback;
static int semaphore;
static uint32_t clock_ms = 100;
static jmp_buf stop_worker;
static unsigned turns;
static unsigned timer_reads, stack_reads;
static uint32_t timer_ticks;
static int test_core;
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
  assert(!task && !locks);
  return 2048U - ++stack_reads;
}
int xPortGetCoreID(void)
{
  assert(locks);
  return test_core;
}
void test_lock(void)
{
  ++timer_ticks;
  ++lock_entries;
  ++locks;
}
void test_unlock(void)
{
  assert(locks);
  --locks;
}
void* heap_caps_malloc(size_t size, uint32_t caps)
{
  assert(!locks && caps == ((UG_S3_RADIO_STORAGE_PSRAM ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL) |
                            MALLOC_CAP_8BIT));
  assert(size == sizeof(espnow_rtcm_storage_data_t));
  ++allocations;
  void* payload = fail_alloc ? NULL : malloc(size);
  payload_begin = (uintptr_t)payload;
  return payload;
}
int64_t esp_timer_get_time(void)
{
  ++timer_reads;
  return (int64_t)clock_ms * 1000 + timer_ticks;
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
  const uint8_t expected[] = {0x7f,
                              0x8d,
                              0x3f,
                              0x26,
                              0xd8,
                              0x5c,
                              0xe3,
                              0x09,
                              0x5b,
                              0x7a,
                              0x3c,
                              0x61,
                              0x79,
                              0x77,
                              0x90,
                              0x6b};
  assert(!memcmp(p, expected, 16));
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
  assert((uintptr_t)bytes + n <= payload_begin ||
         (uintptr_t)bytes >= payload_begin + sizeof(espnow_rtcm_storage_data_t));
  if (expected_count)
    assert(sends < expected_count && n == expected_lengths[sends] &&
           !memcmp(bytes, expected_packets[sends], n));
  ++sends;
  if (!fail_ack)
  {
    const unsigned entries = lock_entries;
    const unsigned timer_before = timer_reads;
    sent(NULL, ESP_NOW_SEND_SUCCESS);
    assert(lock_entries == entries && allocations == 1 && timer_before == timer_reads);
  }
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
static void verify_queue_diagnostics(void)
{
  espnow_rtcm_queue_diagnostics_t d;
  espnow_rtcm_get_queue_diagnostics(&d);
  assert(d.available && !d.oldest_valid && !d.depth && !d.peak && !d.worker_stack_min_bytes);
  assert(d.capacity == ESPNOW_RTCM_QUEUE_DEPTH && !d.observed_core_mask);
  uint8_t bytes[8];
  frame(bytes);
  // Receipt before millisecond wrap, observed after wrap.
  const uint32_t stamp = UINT32_MAX - 9;
  clock_ms = 10;
  assert(espnow_rtcm_send_frame_at(bytes, sizeof(bytes), stamp) == ESP_OK);
  assert(espnow_rtcm_send_frame_at(bytes, sizeof(bytes), clock_ms) == ESP_OK);
  espnow_rtcm_get_queue_diagnostics(&d);
  assert(d.depth == 2 && d.peak == 2 && d.oldest_valid && d.oldest_age_ms == 20);
  test_core = 1;
  clock_ms += 1001;
  step();
  espnow_rtcm_get_queue_diagnostics(&d);
  assert(d.peak == 2 && !d.depth && !d.oldest_valid && d.oldest_age_ms == 0);
  assert(d.max_dequeue_age_ms == 1021 && d.observed_core_mask == 2 && !sends);
  assert(d.worker_stack_min_bytes == 2047 && stack_reads == 1);
  assert(d.max_push_lock_hold_us && d.max_pop_lock_hold_us && d.enter_delay_us_max);
  step();
  assert(stack_reads == 1);
  test_core = 0;
  clock_ms += 1001;
  assert(espnow_rtcm_send_frame_at(bytes, sizeof(bytes), clock_ms) == ESP_OK);
  step();
  espnow_rtcm_get_queue_diagnostics(&d);
  assert(d.observed_core_mask == 3 && d.worker_stack_min_bytes == 2046 && stack_reads == 2);
  assert(espnow_rtcm_send_frame_at(bytes, sizeof(bytes), clock_ms) == ESP_OK);
  espnow_rtcm_invalidate_pending();
  espnow_rtcm_get_queue_diagnostics(&d);
  assert(!d.oldest_valid && !d.depth && d.peak == 2 && d.max_dequeue_age_ms == 1021);
}
static void verify_mailbox_equivalence(void)
{
  espnow_rtcm_mailbox_t original = {0};
  espnow_rtcm_message_t slots[ESPNOW_RTCM_QUEUE_DEPTH] = {0};
  size_t head = 0, count = 0;
  espnow_rtcm_mailbox_view_t view = {slots, &head, &count};
  uint8_t data[RTCM3_MAX_FRAME_LEN];
  for (unsigned round = 0; round < 3; ++round)
  {
    for (unsigned i = 0; i <= ESPNOW_RTCM_QUEUE_DEPTH; ++i)
    {
      memset(data, round * 17 + i, sizeof(data));
      const size_t len = sizeof(data) - i;
      const uint32_t stamp = round * 100 + i;
      bool ok = espnow_rtcm_mailbox_push(&original, data, len, stamp);
      assert(ok == espnow_rtcm_mailbox_view_push(view, data, len, stamp));
      assert(original.head == head && original.count == count);
    }
    const unsigned pops = round == 2 ? ESPNOW_RTCM_QUEUE_DEPTH + 1 : 3;
    for (unsigned i = 0; i < pops; ++i)
    {
      espnow_rtcm_message_t a, b;
      bool ok = espnow_rtcm_mailbox_pop(&original, &a);
      assert(ok == espnow_rtcm_mailbox_view_pop(view, &b));
      if (ok)
        assert(a.stamp_ms == b.stamp_ms && a.len == b.len && !memcmp(a.bytes, b.bytes, a.len));
      assert(original.head == head && original.count == count);
    }
  }
}
int main(int argc, char** argv)
{
  assert(argc == 2);
  if (!strcmp(argv[1], "nominal"))
    verify_mailbox_equivalence();
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
  const bool fail_pmk = !strcmp(argv[1], "pmk-failure");
  ug_test_pmk_failure(fail_pmk);
  if (!strcmp(argv[1], "stop-before-start"))
    espnow_rtcm_stop_until_reboot();
  const int result = espnow_rtcm_start();
  assert(allocations == 1);
  espnow_rtcm_storage_data_t* saved = espnow_rtcm_storage_get(&s_storage);
  if (fail_alloc)
  {
    assert(result == ESP_ERR_NO_MEM && !saved && !sdk_calls);
  }
  else if (fail_wifi || fail_callback || fail_pmk)
  {
    assert(result == ESP_FAIL && saved && !s_started);
  }
  else
  {
    assert(result == ESP_OK && saved && s_started);
    if (!strcmp(argv[1], "queue-diagnostics"))
    {
      verify_queue_diagnostics();
      sends = 0;
    }
    if (!strcmp(argv[1], "max-frame"))
    {
      uint8_t max_frame[RTCM3_MAX_FRAME_LEN];
      memset(max_frame, 0x64, sizeof(max_frame));
      max_frame[0] = 0xd3;
      max_frame[1] = 3;
      max_frame[2] = 255;
      uint32_t crc;
      const size_t body = sizeof(max_frame) - 3;
      assert(ug_rtcm_crc24q(max_frame, body, &crc));
      max_frame[body] = crc >> 16;
      max_frame[body + 1] = crc >> 8;
      max_frame[body + 2] = crc;
      espnow_rtcm_codec_t reference;
      espnow_rtcm_codec_init(&reference);
      reference.transport.next_frame_seq = 17;
      assert(espnow_rtcm_fragment(&reference, max_frame, sizeof(max_frame), save_fragment, NULL) ==
             ESP_OK);
      assert(expected_count > 1);
      assert(espnow_rtcm_send_frame_at(max_frame, sizeof(max_frame), clock_ms) == ESP_OK);
      step();
      assert(sends == expected_count);
      sends = 0;
      expected_count = 0;
    }
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
