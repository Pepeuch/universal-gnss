/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LORA_TRANSPORT_MAGIC 0x4D47U
#define LORA_TRANSPORT_VERSION 1U
#define LORA_TRANSPORT_PACKET_TYPE_RTCM_FRAGMENT 1U
#define LORA_TRANSPORT_DEFAULT_MAX_PAYLOAD 220U
#define LORA_TRANSPORT_MAX_PACKET_LEN 255U
#define LORA_TRANSPORT_MAX_FRAGMENTS 16U

typedef struct
{
  uint16_t magic;
  uint8_t version;
  uint8_t packet_type;
  uint8_t stream_id;
  uint16_t frame_seq;
  uint8_t fragment_index;
  uint8_t fragment_count;
  uint8_t payload_len;
  uint8_t header_checksum;
} lora_transport_header_t;

typedef struct
{
  size_t max_lora_payload;
} lora_transport_config_t;

typedef struct
{
  lora_transport_config_t config;
  uint16_t next_frame_seq;

  uint8_t reassembly_stream_id;
  uint16_t reassembly_frame_seq;
  uint8_t reassembly_fragment_count;
  size_t reassembly_frame_len;
  bool reassembly_received[LORA_TRANSPORT_MAX_FRAGMENTS];
  uint16_t reassembly_fragment_offsets[LORA_TRANSPORT_MAX_FRAGMENTS];
  uint8_t reassembly_fragment_lengths[LORA_TRANSPORT_MAX_FRAGMENTS];
  uint8_t reassembly_buffer[1032];
  bool reassembly_active;
} lora_transport_t;

typedef struct
{
  uint8_t stream_id;
  uint16_t frame_seq;
  uint8_t fragment_index;
  uint8_t fragment_count;
  const uint8_t* packet;
  size_t packet_len;
} lora_transport_fragment_t;

typedef struct
{
  bool complete;
  uint8_t stream_id;
  uint16_t frame_seq;
  const uint8_t* frame;
  size_t frame_len;
} lora_transport_reassembly_result_t;

typedef esp_err_t (*lora_transport_fragment_callback_t)(const lora_transport_fragment_t* fragment,
                                                        void* user_ctx);

uint16_t lora_transport_crc16(const uint8_t* data, size_t len);
size_t lora_transport_max_fragment_payload(size_t max_lora_payload);

void lora_transport_init(lora_transport_t* transport, const lora_transport_config_t* config);
esp_err_t lora_transport_fragment_frame(lora_transport_t* transport,
                                        uint8_t stream_id,
                                        const uint8_t* frame,
                                        size_t frame_len,
                                        lora_transport_fragment_callback_t callback,
                                        void* user_ctx,
                                        uint16_t* frame_seq_out,
                                        uint8_t* fragment_count_out);
esp_err_t lora_transport_reassemble_push(lora_transport_t* transport,
                                         const uint8_t* packet,
                                         size_t packet_len,
                                         lora_transport_reassembly_result_t* result);

#ifdef __cplusplus
}
#endif
