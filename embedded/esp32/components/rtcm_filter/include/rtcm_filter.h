/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "rtcm_profiles.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTCM_FILTER_MAX_RULES 16
#define RTCM3_PREAMBLE 0xD3U
#define RTCM3_HEADER_LEN 3U
#define RTCM3_CRC_LEN 3U
#define RTCM3_MAX_PAYLOAD_LEN 1023U
#define RTCM3_MAX_FRAME_LEN (RTCM3_HEADER_LEN + RTCM3_MAX_PAYLOAD_LEN + RTCM3_CRC_LEN)

typedef enum
{
  RTCM_FILTER_SEND = 0,
  RTCM_FILTER_DROP,
  RTCM_FILTER_DELAY,
} rtcm_filter_decision_t;

typedef struct
{
  uint16_t message_type;
  bool enabled;
  uint32_t min_interval_ms;
  rtcm_priority_t priority;
  uint32_t recommended_max_age_ms;
} rtcm_filter_rule_t;

typedef struct
{
  rtcm_filter_rule_t rule;
  int64_t last_sent_us;
} rtcm_filter_rule_state_t;

typedef struct
{
  rtcm_filter_rule_state_t rules[RTCM_FILTER_MAX_RULES];
  size_t rule_count;
  rtcm_profile_id_t profile_id;
  rtcm_unknown_action_t unknown_action;
} rtcm_filter_t;

typedef struct
{
  rtcm_filter_decision_t decision;
  uint16_t message_type;
  rtcm_priority_t priority;
  uint16_t payload_length;
  uint32_t delay_ms;
  uint32_t recommended_max_age_ms;
  const uint8_t* frame;
  size_t frame_len;
} rtcm_filter_result_t;

typedef struct
{
  uint8_t frame[RTCM3_MAX_FRAME_LEN];
  size_t frame_len;
  size_t expected_len;
  bool synced;
} rtcm3_stream_parser_t;

uint16_t rtcm3_get_message_type(const uint8_t* frame, size_t frame_len);
size_t rtcm3_frame_length(const uint8_t* data, size_t data_len);
uint32_t rtcm3_crc24q(const uint8_t* data, size_t len);

void rtcm3_stream_parser_init(rtcm3_stream_parser_t* parser);
esp_err_t rtcm3_stream_push_byte(rtcm3_stream_parser_t* parser,
                                 uint8_t byte,
                                 const uint8_t** frame,
                                 size_t* frame_len);

void rtcm_filter_init(rtcm_filter_t* filter);
esp_err_t rtcm_filter_init_with_profile(rtcm_filter_t* filter, rtcm_profile_id_t profile_id);
esp_err_t rtcm_filter_reset(rtcm_filter_t* filter);
esp_err_t rtcm_filter_reset_with_profile(rtcm_filter_t* filter, rtcm_profile_id_t profile_id);
esp_err_t rtcm_filter_set_rule(rtcm_filter_t* filter,
                               uint16_t message_type,
                               bool enabled,
                               uint32_t min_interval_ms);
esp_err_t rtcm_filter_get_frame_info(const uint8_t* frame,
                                     size_t frame_len,
                                     uint16_t* message_type,
                                     uint16_t* payload_length);
esp_err_t rtcm_filter_evaluate(rtcm_filter_t* filter,
                               const uint8_t* frame,
                               size_t frame_len,
                               rtcm_filter_result_t* result);
esp_err_t rtcm_filter_mark_sent(rtcm_filter_t* filter, uint16_t message_type);
const char* rtcm_filter_decision_name(rtcm_filter_decision_t decision);

#ifdef __cplusplus
}
#endif
