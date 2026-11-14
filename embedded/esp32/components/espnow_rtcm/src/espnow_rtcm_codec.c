/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#include "espnow_rtcm_codec.h"
#include "ug_rtcm.h"

#include <string.h>

void espnow_rtcm_codec_init(espnow_rtcm_codec_t* codec)
{
  memset(codec, 0, sizeof(*codec));
  lora_transport_config_t config = {.max_lora_payload = ESPNOW_RTCM_PACKET_SIZE};
  lora_transport_init(&codec->transport, &config);
}

void espnow_rtcm_stream_reset(espnow_rtcm_codec_t* codec)
{
  rtcm3_stream_parser_init(&codec->parser);
}

void espnow_rtcm_ntrip_begin(espnow_rtcm_codec_t* codec, bool icy)
{
  espnow_rtcm_stream_reset(codec);
  codec->header_bytes = 0;
  codec->header_match = 0;
  codec->header_separator = icy ? 2 : 4;
}

esp_err_t espnow_rtcm_ntrip_push(espnow_rtcm_codec_t* codec,
                                 const uint8_t* data,
                                 size_t len,
                                 espnow_rtcm_frame_fn sink,
                                 void* ctx)
{
  if (!codec || (!data && len) || !sink)
    return ESP_ERR_INVALID_ARG;
  if (len == 0)
    return ESP_OK;
  const char* delimiter = "\r\n\r\n";
  size_t i = 0;
  while (i < len && codec->header_separator)
  {
    if (++codec->header_bytes > 8192)
      return ESP_ERR_INVALID_RESPONSE;
    uint8_t byte = data[i++];
    if (byte == (uint8_t)delimiter[codec->header_match])
      ++codec->header_match;
    else
      codec->header_match = byte == '\r' ? 1 : 0;
    if (codec->header_match == codec->header_separator)
      codec->header_separator = 0;
  }
  return espnow_rtcm_stream_push(codec, data + i, len - i, sink, ctx);
}

bool espnow_rtcm_frame_valid(const uint8_t* frame, size_t len)
{
  return ug_rtcm_frame_valid(frame, len);
}

esp_err_t espnow_rtcm_stream_push(espnow_rtcm_codec_t* codec,
                                  const uint8_t* data,
                                  size_t len,
                                  espnow_rtcm_frame_fn sink,
                                  void* ctx)
{
  if (!codec || (!data && len) || !sink)
    return ESP_ERR_INVALID_ARG;
  esp_err_t outcome = ESP_OK;
  for (size_t i = 0; i < len; ++i)
  {
    const uint8_t* frame;
    size_t frame_len;
    esp_err_t err = rtcm3_stream_push_byte(&codec->parser, data[i], &frame, &frame_len);
    if (err != ESP_OK)
      outcome = err;
    if (frame)
    {
      if (!espnow_rtcm_frame_valid(frame, frame_len))
        outcome = ESP_ERR_INVALID_CRC;
      else
      {
        err = sink(frame, frame_len, ctx);
        if (err != ESP_OK)
          outcome = err;
      }
    }
  }
  return outcome;
}

esp_err_t espnow_rtcm_fragment(espnow_rtcm_codec_t* codec,
                               const uint8_t* frame,
                               size_t len,
                               lora_transport_fragment_callback_t send,
                               void* ctx)
{
  if (!codec || !send || !espnow_rtcm_frame_valid(frame, len))
    return ESP_ERR_INVALID_ARG;
  return lora_transport_fragment_frame(&codec->transport, 1, frame, len, send, ctx, NULL, NULL);
}

esp_err_t espnow_rtcm_receive(espnow_rtcm_codec_t* codec,
                              const uint8_t* packet,
                              size_t len,
                              uint32_t now_ms,
                              lora_transport_reassembly_result_t* result)
{
  if (!codec || !packet || !result)
    return ESP_ERR_INVALID_ARG;
  memset(result, 0, sizeof(*result));
  const size_t payload_max = lora_transport_max_fragment_payload(ESPNOW_RTCM_PACKET_SIZE);
  if (len < 14 || len > ESPNOW_RTCM_PACKET_SIZE || packet[4] != 1 || packet[8] == 0 ||
      packet[8] > (RTCM3_MAX_FRAME_LEN + payload_max - 1) / payload_max || packet[7] >= packet[8] ||
      packet[9] == 0 || len != (size_t)packet[9] + 13 ||
      (packet[7] + 1 < packet[8] && packet[9] != payload_max))
    return ESP_ERR_INVALID_SIZE;
  uint16_t crc = ((uint16_t)packet[len - 2] << 8) | packet[len - 1];
  if (lora_transport_crc16(packet, len - 2) != crc)
  {
    ++codec->crc16_errors;
    return ESP_ERR_INVALID_CRC;
  }
  /* Invalid headers must not advance the sequence/freshness boundary. */
  uint8_t checksum = 0;
  for (size_t i = 0; i < 10; ++i)
    checksum += packet[i];
  if (packet[0] != (LORA_TRANSPORT_MAGIC >> 8) || packet[1] != (LORA_TRANSPORT_MAGIC & 255) ||
      packet[2] != LORA_TRANSPORT_VERSION ||
      packet[3] != LORA_TRANSPORT_PACKET_TYPE_RTCM_FRAGMENT || packet[10] != checksum)
    return ESP_ERR_INVALID_RESPONSE;
  if (codec->have_sequence && (uint32_t)(now_ms - codec->progress_ms) >= ESPNOW_RTCM_REASSEMBLY_MS)
  {
    if (codec->transport.reassembly_active)
      ++codec->reassembly_expired;
    codec->have_sequence = false;
    codec->transport.reassembly_active = false;
  }
  uint16_t sequence = ((uint16_t)packet[5] << 8) | packet[6];
  if (codec->have_sequence)
  {
    uint16_t advance = (uint16_t)(sequence - codec->sequence);
    if ((advance == 0 && !codec->transport.reassembly_active) || advance >= 0x8000U)
      return ESP_ERR_INVALID_STATE;
    if (advance > 0 && codec->transport.reassembly_active)
      ++codec->reassembly_abandoned;
  }
  esp_err_t err = lora_transport_reassemble_push(&codec->transport, packet, len, result);
  if (err != ESP_OK)
    return err;
  codec->have_sequence = true;
  codec->sequence = sequence;
  codec->progress_ms = now_ms;
  if (result->complete && (!espnow_rtcm_frame_valid(result->frame, result->frame_len) ||
                           packet[8] != (result->frame_len + payload_max - 1) / payload_max))
  {
    ++codec->crc24q_errors;
    memset(result, 0, sizeof(*result));
    return ESP_ERR_INVALID_CRC;
  }
  if (result->complete)
    ++codec->completed_frames;
  return ESP_OK;
}

bool espnow_rtcm_mailbox_push(espnow_rtcm_mailbox_t* box,
                              const uint8_t* data,
                              size_t len,
                              uint32_t now_ms)
{
  if (!box || !data || !len || len > RTCM3_MAX_FRAME_LEN || box->count == ESPNOW_RTCM_QUEUE_DEPTH)
    return false;
  espnow_rtcm_message_t* slot = &box->slots[(box->head + box->count) % ESPNOW_RTCM_QUEUE_DEPTH];
  memcpy(slot->bytes, data, len);
  slot->len = len;
  slot->stamp_ms = now_ms;
  ++box->count;
  return true;
}

bool espnow_rtcm_mailbox_pop(espnow_rtcm_mailbox_t* box, espnow_rtcm_message_t* message)
{
  if (!box || !message || !box->count)
    return false;
  *message = box->slots[box->head];
  box->head = (box->head + 1) % ESPNOW_RTCM_QUEUE_DEPTH;
  --box->count;
  return true;
}
