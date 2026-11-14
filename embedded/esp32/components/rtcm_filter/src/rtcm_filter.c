/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#include "rtcm_filter.h"
#include "ug_rtcm.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "rtcm_filter";

static esp_err_t rtcm_filter_parse_frame(const uint8_t* frame,
                                         size_t frame_len,
                                         uint16_t* message_type,
                                         uint16_t* payload_length);
static rtcm_filter_rule_state_t* rtcm_filter_find_rule(rtcm_filter_t* filter,
                                                       uint16_t message_type);
static rtcm_filter_rule_state_t* rtcm_filter_add_rule(rtcm_filter_t* filter, uint16_t message_type);
static esp_err_t rtcm_filter_apply_profile(rtcm_filter_t* filter, const rtcm_profile_t* profile);

uint16_t rtcm3_get_message_type(const uint8_t* frame, size_t frame_len)
{
  if (frame == NULL || frame_len < (RTCM3_HEADER_LEN + 2U))
  {
    return 0;
  }

  return (uint16_t)((((uint16_t)frame[RTCM3_HEADER_LEN]) << 4) |
                    (((uint16_t)frame[RTCM3_HEADER_LEN + 1]) >> 4));
}

size_t rtcm3_frame_length(const uint8_t* data, size_t data_len)
{
  uint16_t payload_length;

  if (data == NULL || data_len < RTCM3_HEADER_LEN)
  {
    return 0;
  }

  if (data[0] != RTCM3_PREAMBLE)
  {
    return 0;
  }

  payload_length = (uint16_t)(((data[1] & 0x03U) << 8) | data[2]);
  if (payload_length > RTCM3_MAX_PAYLOAD_LEN)
  {
    return 0;
  }

  return (size_t)payload_length + RTCM3_HEADER_LEN + RTCM3_CRC_LEN;
}

uint32_t rtcm3_crc24q(const uint8_t* data, size_t len)
{
  uint32_t crc = 0;
  (void)ug_rtcm_crc24q(data, len, &crc);
  return crc;
}

void rtcm3_stream_parser_init(rtcm3_stream_parser_t* parser)
{
  if (parser == NULL)
  {
    return;
  }

  memset(parser, 0, sizeof(*parser));
}

esp_err_t rtcm3_stream_push_byte(rtcm3_stream_parser_t* parser,
                                 uint8_t byte,
                                 const uint8_t** frame,
                                 size_t* frame_len)
{
  size_t expected_len;
  uint32_t crc_expected;
  uint32_t crc_actual;

  if (parser == NULL || frame == NULL || frame_len == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  *frame = NULL;
  *frame_len = 0;

  if (!parser->synced)
  {
    if (byte != RTCM3_PREAMBLE)
    {
      return ESP_OK;
    }

    parser->synced = true;
    parser->frame[0] = byte;
    parser->frame_len = 1;
    parser->expected_len = 0;
    return ESP_OK;
  }

  if (parser->frame_len >= RTCM3_MAX_FRAME_LEN)
  {
    ESP_LOGW(TAG, "parser overflow, resyncing");
    rtcm3_stream_parser_init(parser);
    return ESP_ERR_INVALID_SIZE;
  }

  parser->frame[parser->frame_len++] = byte;

  if (parser->frame_len == RTCM3_HEADER_LEN)
  {
    expected_len = rtcm3_frame_length(parser->frame, parser->frame_len);
    if (expected_len == 0)
    {
      ESP_LOGW(TAG, "invalid RTCM header, resyncing");
      rtcm3_stream_parser_init(parser);
      return ESP_ERR_INVALID_RESPONSE;
    }

    parser->expected_len = expected_len;
  }

  if (parser->expected_len == 0 || parser->frame_len < parser->expected_len)
  {
    return ESP_OK;
  }

  if (parser->frame_len != parser->expected_len)
  {
    ESP_LOGW(TAG, "frame length mismatch, resyncing");
    rtcm3_stream_parser_init(parser);
    return ESP_ERR_INVALID_SIZE;
  }

  crc_expected = (((uint32_t)parser->frame[parser->frame_len - 3]) << 16) |
                 (((uint32_t)parser->frame[parser->frame_len - 2]) << 8) |
                 ((uint32_t)parser->frame[parser->frame_len - 1]);
  crc_actual = rtcm3_crc24q(parser->frame, parser->frame_len - RTCM3_CRC_LEN);
  if (crc_actual != crc_expected)
  {
    ESP_LOGW(TAG, "crc mismatch, resyncing");
    rtcm3_stream_parser_init(parser);
    return ESP_ERR_INVALID_CRC;
  }

  *frame = parser->frame;
  *frame_len = parser->frame_len;
  parser->synced = false;
  parser->frame_len = 0;
  parser->expected_len = 0;
  return ESP_OK;
}

void rtcm_filter_init(rtcm_filter_t* filter)
{
  (void)rtcm_filter_init_with_profile(filter, RTCM_PROFILE_RTK_MINIMAL);
}

esp_err_t rtcm_filter_init_with_profile(rtcm_filter_t* filter, rtcm_profile_id_t profile_id)
{
  return rtcm_filter_reset_with_profile(filter, profile_id);
}

esp_err_t rtcm_filter_reset(rtcm_filter_t* filter)
{
  return rtcm_filter_reset_with_profile(filter, RTCM_PROFILE_RTK_MINIMAL);
}

esp_err_t rtcm_filter_reset_with_profile(rtcm_filter_t* filter, rtcm_profile_id_t profile_id)
{
  const rtcm_profile_t* profile;

  if (filter == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  profile = rtcm_profile_get(profile_id);
  if (profile == NULL)
  {
    return ESP_ERR_NOT_FOUND;
  }

  memset(filter, 0, sizeof(*filter));
  filter->profile_id = profile->profile_id;
  filter->unknown_action = profile->unknown_action;
  return rtcm_filter_apply_profile(filter, profile);
}

esp_err_t rtcm_filter_set_rule(rtcm_filter_t* filter,
                               uint16_t message_type,
                               bool enabled,
                               uint32_t min_interval_ms)
{
  rtcm_filter_rule_state_t* rule;

  if (filter == NULL || message_type == 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  rule = rtcm_filter_find_rule(filter, message_type);
  if (rule == NULL)
  {
    rule = rtcm_filter_add_rule(filter, message_type);
  }

  if (rule == NULL)
  {
    return ESP_ERR_NO_MEM;
  }

  rule->rule.message_type = message_type;
  rule->rule.enabled = enabled;
  rule->rule.min_interval_ms = min_interval_ms;
  rule->rule.priority = RTCM_PRIORITY_MEDIUM;
  rule->rule.recommended_max_age_ms = min_interval_ms;
  if (!enabled)
  {
    rule->last_sent_us = 0;
  }

  return ESP_OK;
}

esp_err_t rtcm_filter_get_frame_info(const uint8_t* frame,
                                     size_t frame_len,
                                     uint16_t* message_type,
                                     uint16_t* payload_length)
{
  return rtcm_filter_parse_frame(frame, frame_len, message_type, payload_length);
}

esp_err_t rtcm_filter_evaluate(rtcm_filter_t* filter,
                               const uint8_t* frame,
                               size_t frame_len,
                               rtcm_filter_result_t* result)
{
  uint16_t message_type = 0;
  uint16_t payload_length = 0;
  int64_t now_us;
  rtcm_filter_rule_state_t* rule;

  if (filter == NULL || frame == NULL || result == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  memset(result, 0, sizeof(*result));

  ESP_RETURN_ON_ERROR(rtcm_filter_parse_frame(frame, frame_len, &message_type, &payload_length),
                      TAG,
                      "invalid rtcm frame");

  result->message_type = message_type;
  result->payload_length = payload_length;
  result->frame = frame;
  result->frame_len = frame_len;

  rule = rtcm_filter_find_rule(filter, message_type);
  if (rule == NULL || !rule->rule.enabled)
  {
    result->priority = (filter->unknown_action == RTCM_UNKNOWN_ACTION_LOW_PLACEHOLDER)
                           ? RTCM_PRIORITY_LOW
                           : RTCM_PRIORITY_DROP;
    result->recommended_max_age_ms = 0;
    result->decision = RTCM_FILTER_DROP;
    ESP_LOGI(TAG,
             "RTCM type=%u profile=%s priority=%s decision=%s max_age_ms=%" PRIu32,
             message_type,
             rtcm_profile_name(filter->profile_id),
             rtcm_priority_name(result->priority),
             rtcm_filter_decision_name(result->decision),
             result->recommended_max_age_ms);
    return ESP_OK;
  }

  result->priority = rule->rule.priority;
  result->recommended_max_age_ms = rule->rule.recommended_max_age_ms;

  now_us = esp_timer_get_time();
  if (rule->rule.min_interval_ms > 0 && rule->last_sent_us > 0)
  {
    const int64_t min_interval_us = (int64_t)rule->rule.min_interval_ms * 1000;
    const int64_t elapsed_us = now_us - rule->last_sent_us;

    if (elapsed_us < min_interval_us)
    {
      const int64_t remaining_us = min_interval_us - elapsed_us;
      result->decision = RTCM_FILTER_DELAY;
      result->delay_ms = (uint32_t)((remaining_us + 999) / 1000);
      ESP_LOGI(TAG,
               "RTCM type=%u profile=%s priority=%s decision=%s delay_ms=%" PRIu32
               " max_age_ms=%" PRIu32,
               message_type,
               rtcm_profile_name(filter->profile_id),
               rtcm_priority_name(result->priority),
               rtcm_filter_decision_name(result->decision),
               result->delay_ms,
               result->recommended_max_age_ms);
      return ESP_OK;
    }
  }

  result->decision = RTCM_FILTER_SEND;
  ESP_LOGI(TAG,
           "RTCM type=%u profile=%s priority=%s decision=%s max_age_ms=%" PRIu32,
           message_type,
           rtcm_profile_name(filter->profile_id),
           rtcm_priority_name(result->priority),
           rtcm_filter_decision_name(result->decision),
           result->recommended_max_age_ms);
  return ESP_OK;
}

esp_err_t rtcm_filter_mark_sent(rtcm_filter_t* filter, uint16_t message_type)
{
  rtcm_filter_rule_state_t* rule;

  if (filter == NULL || message_type == 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  rule = rtcm_filter_find_rule(filter, message_type);
  if (rule == NULL || !rule->rule.enabled)
  {
    return ESP_ERR_NOT_FOUND;
  }

  rule->last_sent_us = esp_timer_get_time();
  return ESP_OK;
}

static esp_err_t rtcm_filter_parse_frame(const uint8_t* frame,
                                         size_t frame_len,
                                         uint16_t* message_type,
                                         uint16_t* payload_length)
{
  size_t expected_frame_len;
  uint16_t detected_type;
  uint32_t crc_expected;
  uint32_t crc_actual;

  if (frame == NULL || frame_len < (RTCM3_HEADER_LEN + RTCM3_CRC_LEN + 2U))
  {
    return ESP_ERR_INVALID_SIZE;
  }

  expected_frame_len = rtcm3_frame_length(frame, frame_len);
  if (expected_frame_len == 0 || frame_len != expected_frame_len)
  {
    return ESP_ERR_INVALID_SIZE;
  }

  detected_type = rtcm3_get_message_type(frame, frame_len);
  if (detected_type == 0)
  {
    return ESP_ERR_INVALID_RESPONSE;
  }

  crc_expected = (((uint32_t)frame[frame_len - 3]) << 16) |
                 (((uint32_t)frame[frame_len - 2]) << 8) | ((uint32_t)frame[frame_len - 1]);
  crc_actual = rtcm3_crc24q(frame, frame_len - RTCM3_CRC_LEN);
  if (crc_actual != crc_expected)
  {
    return ESP_ERR_INVALID_CRC;
  }

  if (message_type != NULL)
  {
    *message_type = detected_type;
  }
  if (payload_length != NULL)
  {
    *payload_length = (uint16_t)(expected_frame_len - RTCM3_HEADER_LEN - RTCM3_CRC_LEN);
  }

  return ESP_OK;
}

static rtcm_filter_rule_state_t* rtcm_filter_find_rule(rtcm_filter_t* filter, uint16_t message_type)
{
  if (filter == NULL)
  {
    return NULL;
  }

  for (size_t i = 0; i < filter->rule_count; i++)
  {
    if (filter->rules[i].rule.message_type == message_type)
    {
      return &filter->rules[i];
    }
  }

  return NULL;
}

static rtcm_filter_rule_state_t* rtcm_filter_add_rule(rtcm_filter_t* filter, uint16_t message_type)
{
  rtcm_filter_rule_state_t* rule;

  if (filter == NULL || filter->rule_count >= RTCM_FILTER_MAX_RULES)
  {
    return NULL;
  }

  rule = &filter->rules[filter->rule_count++];
  memset(rule, 0, sizeof(*rule));
  rule->rule.message_type = message_type;
  return rule;
}

const char* rtcm_filter_decision_name(rtcm_filter_decision_t decision)
{
  switch (decision)
  {
    case RTCM_FILTER_SEND:
      return "SEND";
    case RTCM_FILTER_DROP:
      return "DROP";
    case RTCM_FILTER_DELAY:
      return "DELAY";
    default:
      return "UNKNOWN";
  }
}

static esp_err_t rtcm_filter_apply_profile(rtcm_filter_t* filter, const rtcm_profile_t* profile)
{
  if (filter == NULL || profile == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  for (size_t i = 0; i < profile->rule_count; i++)
  {
    const rtcm_profile_rule_t* profile_rule = &profile->rules[i];
    rtcm_filter_rule_state_t* rule = rtcm_filter_add_rule(filter, profile_rule->message_type);

    if (rule == NULL)
    {
      return ESP_ERR_NO_MEM;
    }

    rule->rule.message_type = profile_rule->message_type;
    rule->rule.enabled = true;
    rule->rule.min_interval_ms = profile_rule->min_interval_ms;
    rule->rule.priority = profile_rule->priority;
    rule->rule.recommended_max_age_ms = profile_rule->recommended_max_age_ms;
    rule->last_sent_us = 0;
  }

  ESP_LOGI(TAG,
           "RTCM profile=%s rules=%u unknown_action=%s",
           rtcm_profile_name(profile->profile_id),
           (unsigned)profile->rule_count,
           rtcm_unknown_action_name(profile->unknown_action));
  return ESP_OK;
}
