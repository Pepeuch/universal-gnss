/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#include "espnow_rtcm.h"
#include "sdkconfig.h"
#include "ug_pairing.h"

#if CONFIG_ESPNOW_RTCM_BASE || CONFIG_ESPNOW_RTCM_ROVER
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "espnow_rtcm_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static espnow_rtcm_codec_t s_stream, s_radio;
static espnow_rtcm_mailbox_t s_queue;
static espnow_rtcm_message_t s_message; /* Worker-owned, not on the small task stack. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_worker;
static uint8_t s_peer[6];
static uint8_t s_key[16], s_local_mac[6];
static uint8_t s_channel;
static bool s_configured;
#if CONFIG_ESPNOW_RTCM_BASE
static SemaphoreHandle_t s_sent;
static esp_now_send_status_t s_send_status;
static bool s_tx_halted;
static uint32_t s_tx_epoch = 1, s_message_epoch;
#else
static espnow_rtcm_write_fn s_output_writer;
static void* s_output_context;
#endif
static bool s_started;
static espnow_rtcm_stats_t s_stats;
static void counter_add(uint64_t* counter, uint64_t value)
{
  portENTER_CRITICAL(&s_lock);
  *counter += value;
  portEXIT_CRITICAL(&s_lock);
}
void espnow_rtcm_get_stats(espnow_rtcm_stats_t* out)
{
  if (!out)
    return;
  portENTER_CRITICAL(&s_lock);
  *out = s_stats;
  out->started = s_started;
  out->queued = s_queue.count;
#if CONFIG_ESPNOW_RTCM_BASE
  out->tx_quarantined = s_tx_halted;
#endif
  portEXIT_CRITICAL(&s_lock);
}

static uint32_t now_ms(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool queue_push_at(const uint8_t* data, size_t len, uint32_t stamp_ms)
{
  portENTER_CRITICAL(&s_lock);
  bool ok = espnow_rtcm_mailbox_push(&s_queue, data, len, stamp_ms);
  if (!ok)
    ++s_stats.dropped;
  portEXIT_CRITICAL(&s_lock);
  if (ok)
    xTaskNotifyGive(s_worker);
  return ok;
}

static bool queue_push(const uint8_t* data, size_t len)
{
  return queue_push_at(data, len, now_ms());
}

static bool queue_pop(void)
{
  portENTER_CRITICAL(&s_lock);
  bool ok = espnow_rtcm_mailbox_pop(&s_queue, &s_message);
#if CONFIG_ESPNOW_RTCM_BASE
  s_message_epoch = s_tx_epoch;
#endif
  portEXIT_CRITICAL(&s_lock);
  return ok;
}

#if CONFIG_ESPNOW_RTCM_BASE
static void sent(const esp_now_send_info_t* info, esp_now_send_status_t status)
{
  (void)info;
  s_send_status = status;
  xSemaphoreGive(s_sent);
}

static esp_err_t send_fragment(const lora_transport_fragment_t* part, void* ctx)
{
  const uint32_t epoch = *(const uint32_t*)ctx;
  portENTER_CRITICAL(&s_lock);
  const bool invalidated = epoch != s_tx_epoch || s_tx_halted;
  portEXIT_CRITICAL(&s_lock);
  if ((uint32_t)(now_ms() - s_message.stamp_ms) > 1000)
    return ESP_ERR_TIMEOUT;
  if (invalidated)
    return ESP_ERR_INVALID_STATE;
  esp_err_t err = esp_now_send(s_peer, part->packet, part->packet_len);
  if (err != ESP_OK)
  {
    counter_add(&s_stats.tx_failures, 1);
    return err;
  }
  if (xSemaphoreTake(s_sent, pdMS_TO_TICKS(200)) != pdTRUE)
  {
    /* A late callback must never acknowledge a different packet. Reboot to resume. */
    portENTER_CRITICAL(&s_lock);
    s_tx_halted = true;
    portEXIT_CRITICAL(&s_lock);
    counter_add(&s_stats.tx_failures, 1);
    return ESP_ERR_TIMEOUT;
  }
  counter_add(s_send_status == ESP_NOW_SEND_SUCCESS ? &s_stats.tx_packets_acknowledged
                                                    : &s_stats.tx_failures,
              1);
  return s_send_status == ESP_NOW_SEND_SUCCESS ? ESP_OK : ESP_FAIL;
}

static esp_err_t enqueue_frame(const uint8_t* frame, size_t len, void* ctx)
{
  (void)ctx;
  return queue_push(frame, len) ? ESP_OK : ESP_ERR_NO_MEM;
}
#else
static void received(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
  if (!info || !data || len <= 0 || len > ESPNOW_RTCM_PACKET_SIZE ||
      !ug_pairing_sender(s_peer, info->src_addr, info->des_addr, s_local_mac))
    return;
  counter_add(&s_stats.rx_packets, 1);
  /* Wi-Fi callback only copies one bounded packet; never performs USB I/O. */
  queue_push(data, (size_t)len);
}

static int usb_write(const uint8_t* bytes, size_t size, uint32_t wait_ms, void* context)
{
  (void)context;
  return usb_serial_jtag_write_bytes(bytes, size, pdMS_TO_TICKS(wait_ms));
}
static uint32_t output_clock(void* context)
{
  (void)context;
  return now_ms();
}
static void output_idle(void* context)
{
  (void)context;
  vTaskDelay(1);
}
static void output_frame(const uint8_t* frame, size_t len)
{
  const espnow_rtcm_output_ops_t ops = {
      s_output_writer ? s_output_writer : usb_write, output_clock, output_idle, s_output_context};
  const espnow_rtcm_output_result_t result = espnow_rtcm_output_frame(&ops, frame, len);
  counter_add(&s_stats.output_bytes, result.accepted);
  counter_add(result.complete ? &s_stats.output_frames : &s_stats.output_errors, 1);
  // Driver acceptance is not physical USB/UART delivery; stalls can truncate.
  if (!s_output_writer)
  {
    counter_add(&s_stats.usb_bytes, result.accepted);
    counter_add(result.complete ? &s_stats.usb_frames : &s_stats.usb_errors, 1);
  }
}
#endif
esp_err_t espnow_rtcm_set_rover_writer(espnow_rtcm_write_fn writer, void* context)
{
#if CONFIG_ESPNOW_RTCM_ROVER
  if (s_started || s_worker || !writer)
    return ESP_ERR_INVALID_STATE;
  s_output_writer = writer;
  s_output_context = context;
  return ESP_OK;
#else
  (void)writer;
  (void)context;
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void worker(void* arg)
{
  (void)arg;
  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    while (queue_pop())
    {
      if ((uint32_t)(now_ms() - s_message.stamp_ms) > 1000)
      {
        counter_add(&s_stats.dropped, 1);
        continue;
      }
#if CONFIG_ESPNOW_RTCM_BASE
      const uint32_t epoch = s_message_epoch;
      if (espnow_rtcm_fragment(
              &s_radio, s_message.bytes, s_message.len, send_fragment, (void*)&epoch) != ESP_OK)
        counter_add(&s_stats.invalid, 1);
      else
        counter_add(&s_stats.tx_frames, 1);
#else
      lora_transport_reassembly_result_t result;
      if (espnow_rtcm_receive(&s_radio, s_message.bytes, s_message.len, now_ms(), &result) !=
          ESP_OK)
        counter_add(&s_stats.invalid, 1);
      else if (result.complete)
      {
        counter_add(&s_stats.rx_frames, 1);
        output_frame(result.frame, result.frame_len);
      }
      portENTER_CRITICAL(&s_lock);
      s_stats.rx_crc16_errors = s_radio.crc16_errors;
      s_stats.rx_crc24q_errors = s_radio.crc24q_errors;
      s_stats.reassembly_expired = s_radio.reassembly_expired;
      s_stats.reassembly_abandoned = s_radio.reassembly_abandoned;
      portEXIT_CRITICAL(&s_lock);
#endif
    }
  }
}

esp_err_t espnow_rtcm_configure_secure(const char* peer_mac, const char* key, uint8_t channel)
{
  if (s_started || s_worker)
    return ESP_ERR_INVALID_STATE;
  ug_pairing_t pairing;
  if (!ug_pairing_from_text(peer_mac, key, channel, &pairing))
    return ESP_ERR_INVALID_ARG;
  memcpy(s_peer, pairing.peer, sizeof(s_peer));
  memcpy(s_key, pairing.key, sizeof(s_key));
  s_channel = pairing.channel;
  s_configured = true;
  return ESP_OK;
}
esp_err_t espnow_rtcm_start(void)
{
  if (s_started || !s_configured)
    return ESP_ERR_INVALID_STATE;
  espnow_rtcm_codec_init(&s_stream);
  espnow_rtcm_codec_init(&s_radio);
  s_radio.transport.next_frame_seq = (uint16_t)esp_random();
#if CONFIG_ESPNOW_RTCM_BASE
  s_sent = xSemaphoreCreateBinary();
  if (!s_sent)
    return ESP_ERR_NO_MEM;
#endif
#if CONFIG_ESPNOW_RTCM_ROVER
  esp_err_t err = ESP_OK;
  if (!s_output_writer && !usb_serial_jtag_is_driver_installed())
  {
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 2048, .rx_buffer_size = 256};
    err = usb_serial_jtag_driver_install(&usb);
    if (err != ESP_OK)
      return err;
  }
#else
  esp_err_t err;
#endif
  wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
  wifi.nvs_enable = false;
  wifi_mode_t existing_mode;
  bool existing_wifi = esp_wifi_get_mode(&existing_mode) == ESP_OK;
  if (!existing_wifi && (err = esp_wifi_init(&wifi)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
  if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
  if ((err = esp_wifi_set_mode(
           existing_wifi && (existing_mode == WIFI_MODE_AP || existing_mode == WIFI_MODE_APSTA)
               ? WIFI_MODE_APSTA
               : WIFI_MODE_STA)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
  if (!existing_wifi && (err = esp_wifi_start()) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
  if ((err = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
  if ((err = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
  if ((err = esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
  uint8_t actual_channel = 0, sta_mac[6] = {0};
  /* Initialize the output before reading and verifying the effective channel. */
  wifi_second_chan_t secondary_channel = WIFI_SECOND_CHAN_NONE;
  if ((err = esp_wifi_get_channel(&actual_channel, &secondary_channel)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
#if CONFIG_ESPNOW_RTCM_BASE
  ESP_LOGI("ESP_NOW_RTCM",
           "Channel requested %u, observed %u, secondary %u",
           s_channel,
           actual_channel,
           secondary_channel);
#endif
  if (actual_channel != s_channel || secondary_channel != WIFI_SECOND_CHAN_NONE)
    return ESP_ERR_INVALID_STATE;
  if ((err = esp_wifi_get_mac(WIFI_IF_STA, sta_mac)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
#if CONFIG_ESPNOW_RTCM_BASE
  /* BASE diagnostics only: the rover USB port remains a binary RTCM stream. */
  ESP_LOGI("ESP_NOW_RTCM",
           "STA %02x:%02x:%02x:%02x:%02x:%02x, channel %u, HT20",
           sta_mac[0],
           sta_mac[1],
           sta_mac[2],
           sta_mac[3],
           sta_mac[4],
           sta_mac[5],
           actual_channel);
#endif
  if ((err = esp_now_init()) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
  uint8_t pmk[16];
  esp_fill_random(pmk, sizeof(pmk));
  if ((err = esp_now_set_pmk(pmk)) != ESP_OK ||
      (err = esp_wifi_get_mac(WIFI_IF_STA, s_local_mac)) != ESP_OK)
    return err;
  memset(pmk, 0, sizeof(pmk));
  esp_now_peer_info_t peer = {.channel = s_channel, .ifidx = WIFI_IF_STA, .encrypt = true};
  memcpy(peer.lmk, s_key, sizeof(s_key));
  memcpy(peer.peer_addr, s_peer, sizeof(s_peer));
  if ((err = esp_now_add_peer(&peer)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
  if (xTaskCreate(worker, "espnow_rtcm", 4096, NULL, 4, &s_worker) != pdPASS)
    return ESP_ERR_NO_MEM;
#if CONFIG_ESPNOW_RTCM_BASE
  if ((err = esp_now_register_send_cb(sent)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
#else
  if ((err = esp_now_register_recv_cb(received)) != ESP_OK)
  {
#if CONFIG_ESPNOW_RTCM_BASE
    ESP_LOGE("ESP_NOW_RTCM", "Setup failed at line %d: %s", __LINE__, esp_err_to_name(err));
#endif
    return err;
  }
#endif
  portENTER_CRITICAL(&s_lock);
  s_stats.effective_channel = actual_channel;
  s_started = true;
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}

esp_err_t espnow_rtcm_send_frame(const uint8_t* frame, size_t len)
{
  return espnow_rtcm_send_frame_at(frame, len, now_ms());
}
esp_err_t espnow_rtcm_send_frame_at(const uint8_t* frame, size_t len, uint32_t receipt_ms)
{
#if CONFIG_ESPNOW_RTCM_BASE
  if (!s_started || !espnow_rtcm_frame_valid(frame, len) ||
      (uint32_t)(now_ms() - receipt_ms) > 1000)
    return ESP_ERR_INVALID_ARG;
  return queue_push_at(frame, len, receipt_ms) ? ESP_OK : ESP_ERR_NO_MEM;
#else
  (void)frame;
  (void)len;
  (void)receipt_ms;
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool espnow_rtcm_configuration_matches(const char* peer, const char* key, uint8_t channel)
{
  ug_pairing_t candidate;
  if (!ug_pairing_from_text(peer, key, channel, &candidate))
    return false;
  portENTER_CRITICAL(&s_lock);
  const bool matches = s_started && s_channel == channel && !memcmp(candidate.peer, s_peer, 6) &&
                       !memcmp(candidate.key, s_key, 16);
  portEXIT_CRITICAL(&s_lock);
  return matches;
}
void espnow_rtcm_stop_until_reboot(void)
{
#if CONFIG_ESPNOW_RTCM_BASE
  portENTER_CRITICAL(&s_lock);
  s_tx_halted = true;
  s_stats.dropped += s_queue.count;
  s_queue.head = s_queue.count = 0;
  portEXIT_CRITICAL(&s_lock);
#endif
}

void espnow_rtcm_invalidate_pending(void)
{
#if CONFIG_ESPNOW_RTCM_BASE
  portENTER_CRITICAL(&s_lock);
  if (s_tx_epoch == UINT32_MAX)
    s_tx_halted = true;
  else
    ++s_tx_epoch;
  s_stats.dropped += s_queue.count;
  s_queue.head = s_queue.count = 0;
  portEXIT_CRITICAL(&s_lock);
#endif
}

void espnow_rtcm_ntrip_reset(void)
{
#if CONFIG_ESPNOW_RTCM_BASE
  espnow_rtcm_stream_reset(&s_stream);
  portENTER_CRITICAL(&s_lock);
  s_queue.head = s_queue.count = 0;
  portEXIT_CRITICAL(&s_lock);
#endif
}

void espnow_rtcm_ntrip_response(bool icy)
{
#if CONFIG_ESPNOW_RTCM_BASE
  espnow_rtcm_ntrip_begin(&s_stream, icy);
#else
  (void)icy;
#endif
}

void espnow_rtcm_ntrip_bytes(const uint8_t* data, size_t len)
{
#if CONFIG_ESPNOW_RTCM_BASE
  if (s_started)
    (void)espnow_rtcm_ntrip_push(&s_stream, data, len, enqueue_frame, NULL);
#else
  (void)data;
  (void)len;
#endif
}
#else
void espnow_rtcm_get_stats(espnow_rtcm_stats_t* out)
{
  if (out)
  {
    const espnow_rtcm_stats_t empty = {0};
    *out = empty;
  }
}
esp_err_t espnow_rtcm_configure_secure(const char* peer_mac, const char* key, uint8_t channel)
{
  (void)peer_mac;
  (void)key;
  (void)channel;
  return ESP_ERR_NOT_SUPPORTED;
}
bool espnow_rtcm_configuration_matches(const char* peer, const char* key, uint8_t channel)
{
  (void)peer;
  (void)key;
  (void)channel;
  return false;
}
void espnow_rtcm_stop_until_reboot(void)
{
}
void espnow_rtcm_invalidate_pending(void)
{
}
esp_err_t espnow_rtcm_start(void)
{
  return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t espnow_rtcm_send_frame_at(const uint8_t* frame, size_t len, uint32_t receipt_ms)
{
  (void)frame;
  (void)len;
  (void)receipt_ms;
  return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t espnow_rtcm_send_frame(const uint8_t* frame, size_t len)
{
  (void)frame;
  (void)len;
  return ESP_ERR_NOT_SUPPORTED;
}
void espnow_rtcm_ntrip_reset(void)
{
}
void espnow_rtcm_ntrip_response(bool icy)
{
  (void)icy;
}
void espnow_rtcm_ntrip_bytes(const uint8_t* data, size_t len)
{
  (void)data;
  (void)len;
}
#endif
