/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#pragma once

#include "lora_transport.h"
#include "rtcm_filter.h"

#define ESPNOW_RTCM_PACKET_SIZE 220U
#define ESPNOW_RTCM_QUEUE_DEPTH 8U
#define ESPNOW_RTCM_REASSEMBLY_MS 500U

/* Single-owner codec; callbacks borrow buffers only until they return. */
typedef esp_err_t (*espnow_rtcm_frame_fn)(const uint8_t*, size_t, void*);
typedef struct
{
  rtcm3_stream_parser_t parser;
  lora_transport_t transport;
  bool have_sequence;
  uint16_t sequence;
  uint32_t progress_ms;
  size_t header_bytes;
  uint8_t header_match, header_separator;
} espnow_rtcm_codec_t;

typedef struct
{
  uint32_t stamp_ms;
  size_t len;
  uint8_t bytes[RTCM3_MAX_FRAME_LEN];
} espnow_rtcm_message_t;

/* Nonblocking whole-message FIFO; runtime supplies its own short critical section. */
typedef struct
{
  espnow_rtcm_message_t slots[ESPNOW_RTCM_QUEUE_DEPTH];
  size_t head, count;
} espnow_rtcm_mailbox_t;

void espnow_rtcm_codec_init(espnow_rtcm_codec_t* codec);
void espnow_rtcm_stream_reset(espnow_rtcm_codec_t* codec);
void espnow_rtcm_ntrip_begin(espnow_rtcm_codec_t* codec, bool icy);
esp_err_t espnow_rtcm_ntrip_push(espnow_rtcm_codec_t* codec,
                                 const uint8_t* data,
                                 size_t len,
                                 espnow_rtcm_frame_fn sink,
                                 void* ctx);
bool espnow_rtcm_frame_valid(const uint8_t* frame, size_t len);
esp_err_t espnow_rtcm_stream_push(espnow_rtcm_codec_t* codec,
                                  const uint8_t* data,
                                  size_t len,
                                  espnow_rtcm_frame_fn sink,
                                  void* ctx);
esp_err_t espnow_rtcm_fragment(espnow_rtcm_codec_t* codec,
                               const uint8_t* frame,
                               size_t len,
                               lora_transport_fragment_callback_t send,
                               void* ctx);
esp_err_t espnow_rtcm_receive(espnow_rtcm_codec_t* codec,
                              const uint8_t* packet,
                              size_t len,
                              uint32_t now_ms,
                              lora_transport_reassembly_result_t* result);
bool espnow_rtcm_mailbox_push(espnow_rtcm_mailbox_t* box,
                              const uint8_t* data,
                              size_t len,
                              uint32_t now_ms);
bool espnow_rtcm_mailbox_pop(espnow_rtcm_mailbox_t* box, espnow_rtcm_message_t* message);
