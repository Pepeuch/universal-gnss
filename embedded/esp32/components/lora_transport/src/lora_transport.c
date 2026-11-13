/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#include "lora_transport.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "rtcm_filter.h"

static const char* TAG = "lora_transport";

#define LORA_TRANSPORT_HEADER_SIZE 11U
#define LORA_TRANSPORT_CRC_SIZE 2U
#define LORA_TRANSPORT_OVERHEAD (LORA_TRANSPORT_HEADER_SIZE + LORA_TRANSPORT_CRC_SIZE)

static uint8_t lora_transport_header_checksum(const uint8_t* header, size_t len);
static void lora_transport_write_u16_be(uint8_t* dst, uint16_t value);
static uint16_t lora_transport_read_u16_be(const uint8_t* src);
static void lora_transport_reset_reassembly(lora_transport_t* transport);
static esp_err_t lora_transport_parse_header(const uint8_t* packet,
                                             size_t packet_len,
                                             lora_transport_header_t* header,
                                             uint16_t* payload_crc);
static bool lora_transport_all_fragments_received(const lora_transport_t* transport);

uint16_t lora_transport_crc16(const uint8_t* data, size_t len)
{
  uint16_t crc = 0xFFFFU;

  if (data == NULL)
  {
    return 0;
  }

  for (size_t i = 0; i < len; i++)
  {
    crc ^= (uint16_t)data[i] << 8;
    for (int bit = 0; bit < 8; bit++)
    {
      if ((crc & 0x8000U) != 0U)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      }
      else
      {
        crc <<= 1;
      }
    }
  }

  return crc;
}

size_t lora_transport_max_fragment_payload(size_t max_lora_payload)
{
  if (max_lora_payload <= LORA_TRANSPORT_OVERHEAD)
  {
    return 0;
  }

  if (max_lora_payload > LORA_TRANSPORT_MAX_PACKET_LEN)
  {
    max_lora_payload = LORA_TRANSPORT_MAX_PACKET_LEN;
  }

  return max_lora_payload - LORA_TRANSPORT_OVERHEAD;
}

void lora_transport_init(lora_transport_t* transport, const lora_transport_config_t* config)
{
  if (transport == NULL)
  {
    return;
  }

  memset(transport, 0, sizeof(*transport));
  transport->config.max_lora_payload = LORA_TRANSPORT_DEFAULT_MAX_PAYLOAD;

  if (config != NULL && config->max_lora_payload != 0)
  {
    transport->config.max_lora_payload = config->max_lora_payload;
  }

  if (transport->config.max_lora_payload > LORA_TRANSPORT_MAX_PACKET_LEN)
  {
    transport->config.max_lora_payload = LORA_TRANSPORT_MAX_PACKET_LEN;
  }
}

esp_err_t lora_transport_fragment_frame(lora_transport_t* transport,
                                        uint8_t stream_id,
                                        const uint8_t* frame,
                                        size_t frame_len,
                                        lora_transport_fragment_callback_t callback,
                                        void* user_ctx,
                                        uint16_t* frame_seq_out,
                                        uint8_t* fragment_count_out)
{
  uint16_t frame_seq;
  size_t fragment_payload_max;
  size_t fragment_count;
  uint8_t packet[LORA_TRANSPORT_MAX_PACKET_LEN];

  if (transport == NULL || frame == NULL || frame_len == 0 || callback == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (frame_len > RTCM3_MAX_FRAME_LEN)
  {
    ESP_LOGW(TAG, "frame too large for RTCM transport: len=%u", (unsigned)frame_len);
    return ESP_ERR_INVALID_SIZE;
  }

  fragment_payload_max = lora_transport_max_fragment_payload(transport->config.max_lora_payload);
  if (fragment_payload_max == 0)
  {
    return ESP_ERR_INVALID_SIZE;
  }

  fragment_count = (frame_len + fragment_payload_max - 1U) / fragment_payload_max;
  if (fragment_count == 0 || fragment_count > LORA_TRANSPORT_MAX_FRAGMENTS)
  {
    ESP_LOGW(TAG,
             "frame len=%u exceeds fragmentation capacity payload_max=%u fragments=%u",
             (unsigned)frame_len,
             (unsigned)fragment_payload_max,
             (unsigned)fragment_count);
    return ESP_ERR_INVALID_SIZE;
  }

  frame_seq = transport->next_frame_seq++;
  for (size_t fragment_index = 0; fragment_index < fragment_count; fragment_index++)
  {
    size_t offset = fragment_index * fragment_payload_max;
    size_t payload_len = frame_len - offset;
    uint16_t packet_crc;
    lora_transport_fragment_t fragment = {0};

    if (payload_len > fragment_payload_max)
    {
      payload_len = fragment_payload_max;
    }

    lora_transport_write_u16_be(&packet[0], LORA_TRANSPORT_MAGIC);
    packet[2] = LORA_TRANSPORT_VERSION;
    packet[3] = LORA_TRANSPORT_PACKET_TYPE_RTCM_FRAGMENT;
    packet[4] = stream_id;
    lora_transport_write_u16_be(&packet[5], frame_seq);
    packet[7] = (uint8_t)fragment_index;
    packet[8] = (uint8_t)fragment_count;
    packet[9] = (uint8_t)payload_len;
    packet[10] = lora_transport_header_checksum(packet, 10);

    memcpy(&packet[LORA_TRANSPORT_HEADER_SIZE], frame + offset, payload_len);
    packet_crc = lora_transport_crc16(packet, LORA_TRANSPORT_HEADER_SIZE + payload_len);
    lora_transport_write_u16_be(&packet[LORA_TRANSPORT_HEADER_SIZE + payload_len], packet_crc);

    fragment.stream_id = stream_id;
    fragment.frame_seq = frame_seq;
    fragment.fragment_index = (uint8_t)fragment_index;
    fragment.fragment_count = (uint8_t)fragment_count;
    fragment.packet = packet;
    fragment.packet_len = LORA_TRANSPORT_HEADER_SIZE + payload_len + LORA_TRANSPORT_CRC_SIZE;

    ESP_LOGI(TAG,
             "fragment frame_seq=%u fragment=%u/%u payload=%u",
             frame_seq,
             (unsigned)fragment_index + 1U,
             (unsigned)fragment_count,
             (unsigned)payload_len);

    ESP_RETURN_ON_ERROR(callback(&fragment, user_ctx), TAG, "fragment callback failed");
  }

  if (frame_seq_out != NULL)
  {
    *frame_seq_out = frame_seq;
  }
  if (fragment_count_out != NULL)
  {
    *fragment_count_out = (uint8_t)fragment_count;
  }

  return ESP_OK;
}

esp_err_t lora_transport_reassemble_push(lora_transport_t* transport,
                                         const uint8_t* packet,
                                         size_t packet_len,
                                         lora_transport_reassembly_result_t* result)
{
  lora_transport_header_t header;
  uint16_t packet_crc;
  uint16_t expected_crc;
  size_t payload_offset = LORA_TRANSPORT_HEADER_SIZE;
  size_t fragment_offset;

  if (transport == NULL || packet == NULL || result == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  memset(result, 0, sizeof(*result));

  ESP_RETURN_ON_ERROR(
      lora_transport_parse_header(packet, packet_len, &header, &packet_crc), TAG, "invalid packet");

  expected_crc = lora_transport_crc16(packet, packet_len - LORA_TRANSPORT_CRC_SIZE);
  if (expected_crc != packet_crc)
  {
    ESP_LOGW(TAG,
             "drop fragment stream=%u frame_seq=%u fragment=%u/%u reason=crc16",
             header.stream_id,
             header.frame_seq,
             header.fragment_index + 1U,
             header.fragment_count);
    return ESP_ERR_INVALID_CRC;
  }

  if (!transport->reassembly_active || transport->reassembly_stream_id != header.stream_id ||
      transport->reassembly_frame_seq != header.frame_seq ||
      transport->reassembly_fragment_count != header.fragment_count)
  {
    if (transport->reassembly_active)
    {
      ESP_LOGW(TAG,
               "drop incomplete frame stream=%u frame_seq=%u received_new_seq=%u",
               transport->reassembly_stream_id,
               transport->reassembly_frame_seq,
               header.frame_seq);
    }

    lora_transport_reset_reassembly(transport);
    transport->reassembly_active = true;
    transport->reassembly_stream_id = header.stream_id;
    transport->reassembly_frame_seq = header.frame_seq;
    transport->reassembly_fragment_count = header.fragment_count;
  }

  if (transport->reassembly_received[header.fragment_index])
  {
    ESP_LOGW(TAG,
             "drop fragment stream=%u frame_seq=%u fragment=%u/%u reason=duplicate",
             header.stream_id,
             header.frame_seq,
             header.fragment_index + 1U,
             header.fragment_count);
    return ESP_ERR_INVALID_STATE;
  }

  fragment_offset = (size_t)header.fragment_index *
                    lora_transport_max_fragment_payload(transport->config.max_lora_payload);
  if ((fragment_offset + header.payload_len) > sizeof(transport->reassembly_buffer))
  {
    ESP_LOGW(TAG,
             "drop fragment stream=%u frame_seq=%u fragment=%u/%u reason=oversize",
             header.stream_id,
             header.frame_seq,
             header.fragment_index + 1U,
             header.fragment_count);
    lora_transport_reset_reassembly(transport);
    return ESP_ERR_INVALID_SIZE;
  }

  memcpy(
      transport->reassembly_buffer + fragment_offset, packet + payload_offset, header.payload_len);
  transport->reassembly_received[header.fragment_index] = true;
  transport->reassembly_fragment_offsets[header.fragment_index] = (uint16_t)fragment_offset;
  transport->reassembly_fragment_lengths[header.fragment_index] = header.payload_len;

  if ((fragment_offset + header.payload_len) > transport->reassembly_frame_len)
  {
    transport->reassembly_frame_len = fragment_offset + header.payload_len;
  }

  ESP_LOGI(TAG,
           "rx fragment frame_seq=%u fragment=%u/%u payload=%u",
           header.frame_seq,
           header.fragment_index + 1U,
           header.fragment_count,
           header.payload_len);

  if (!lora_transport_all_fragments_received(transport))
  {
    return ESP_OK;
  }

  result->complete = true;
  result->stream_id = transport->reassembly_stream_id;
  result->frame_seq = transport->reassembly_frame_seq;
  result->frame = transport->reassembly_buffer;
  result->frame_len = transport->reassembly_frame_len;

  ESP_LOGI(TAG,
           "reassembled frame_seq=%u fragments=%u len=%u",
           result->frame_seq,
           transport->reassembly_fragment_count,
           (unsigned)result->frame_len);

  transport->reassembly_active = false;
  return ESP_OK;
}

static uint8_t lora_transport_header_checksum(const uint8_t* header, size_t len)
{
  uint8_t checksum = 0;

  if (header == NULL)
  {
    return 0;
  }

  for (size_t i = 0; i < len; i++)
  {
    checksum = (uint8_t)(checksum + header[i]);
  }

  return checksum;
}

static void lora_transport_write_u16_be(uint8_t* dst, uint16_t value)
{
  dst[0] = (uint8_t)(value >> 8);
  dst[1] = (uint8_t)(value & 0xFFU);
}

static uint16_t lora_transport_read_u16_be(const uint8_t* src)
{
  return (uint16_t)((((uint16_t)src[0]) << 8) | src[1]);
}

static void lora_transport_reset_reassembly(lora_transport_t* transport)
{
  transport->reassembly_active = false;
  transport->reassembly_stream_id = 0;
  transport->reassembly_frame_seq = 0;
  transport->reassembly_fragment_count = 0;
  transport->reassembly_frame_len = 0;
  memset(transport->reassembly_received, 0, sizeof(transport->reassembly_received));
  memset(transport->reassembly_fragment_offsets, 0, sizeof(transport->reassembly_fragment_offsets));
  memset(transport->reassembly_fragment_lengths, 0, sizeof(transport->reassembly_fragment_lengths));
}

static esp_err_t lora_transport_parse_header(const uint8_t* packet,
                                             size_t packet_len,
                                             lora_transport_header_t* header,
                                             uint16_t* payload_crc)
{
  uint8_t header_checksum;

  if (packet == NULL || header == NULL || payload_crc == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (packet_len < (LORA_TRANSPORT_OVERHEAD + 1U) || packet_len > LORA_TRANSPORT_MAX_PACKET_LEN)
  {
    return ESP_ERR_INVALID_SIZE;
  }

  header->magic = lora_transport_read_u16_be(&packet[0]);
  header->version = packet[2];
  header->packet_type = packet[3];
  header->stream_id = packet[4];
  header->frame_seq = lora_transport_read_u16_be(&packet[5]);
  header->fragment_index = packet[7];
  header->fragment_count = packet[8];
  header->payload_len = packet[9];
  header->header_checksum = packet[10];

  if (header->magic != LORA_TRANSPORT_MAGIC || header->version != LORA_TRANSPORT_VERSION ||
      header->packet_type != LORA_TRANSPORT_PACKET_TYPE_RTCM_FRAGMENT)
  {
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (header->fragment_count == 0 || header->fragment_count > LORA_TRANSPORT_MAX_FRAGMENTS ||
      header->fragment_index >= header->fragment_count)
  {
    return ESP_ERR_INVALID_SIZE;
  }

  if ((size_t)header->payload_len != (packet_len - LORA_TRANSPORT_OVERHEAD))
  {
    return ESP_ERR_INVALID_SIZE;
  }

  header_checksum = lora_transport_header_checksum(packet, 10);
  if (header_checksum != header->header_checksum)
  {
    return ESP_ERR_INVALID_CRC;
  }

  *payload_crc = lora_transport_read_u16_be(packet + packet_len - LORA_TRANSPORT_CRC_SIZE);
  return ESP_OK;
}

static bool lora_transport_all_fragments_received(const lora_transport_t* transport)
{
  for (uint8_t i = 0; i < transport->reassembly_fragment_count; i++)
  {
    if (!transport->reassembly_received[i])
    {
      return false;
    }
  }

  return true;
}
