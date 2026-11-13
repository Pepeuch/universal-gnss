/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  RTCM_PRIORITY_HIGH = 0,
  RTCM_PRIORITY_MEDIUM,
  RTCM_PRIORITY_LOW,
  RTCM_PRIORITY_DROP,
} rtcm_priority_t;

typedef enum
{
  RTCM_PROFILE_RTK_MINIMAL = 0,
  RTCM_PROFILE_RTK_GPS_ONLY,
  RTCM_PROFILE_RTK_FULL,
  RTCM_PROFILE_CUSTOM,
} rtcm_profile_id_t;

typedef enum
{
  RTCM_UNKNOWN_ACTION_DROP = 0,
  RTCM_UNKNOWN_ACTION_LOW_PLACEHOLDER,
} rtcm_unknown_action_t;

typedef struct
{
  uint16_t message_type;
  rtcm_priority_t priority;
  uint32_t recommended_max_age_ms;
  uint32_t min_interval_ms;
} rtcm_profile_rule_t;

typedef struct
{
  rtcm_profile_id_t profile_id;
  const char* name;
  const rtcm_profile_rule_t* rules;
  size_t rule_count;
  rtcm_unknown_action_t unknown_action;
} rtcm_profile_t;

const rtcm_profile_t* rtcm_profile_get(rtcm_profile_id_t profile_id);
const char* rtcm_profile_name(rtcm_profile_id_t profile_id);
const char* rtcm_priority_name(rtcm_priority_t priority);
const char* rtcm_unknown_action_name(rtcm_unknown_action_t action);

#ifdef __cplusplus
}
#endif
