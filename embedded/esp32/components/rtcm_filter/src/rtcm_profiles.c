/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#include "rtcm_profiles.h"

static const rtcm_profile_rule_t s_profile_rtk_minimal_rules[] = {
    {1077, RTCM_PRIORITY_HIGH, 2000, 0},
    {1087, RTCM_PRIORITY_HIGH, 2000, 0},
    {1005, RTCM_PRIORITY_MEDIUM, 10000, 10000},
    {1006, RTCM_PRIORITY_MEDIUM, 10000, 10000},
    {1230, RTCM_PRIORITY_MEDIUM, 10000, 10000},
};

static const rtcm_profile_rule_t s_profile_rtk_gps_only_rules[] = {
    {1077, RTCM_PRIORITY_HIGH, 2000, 0},
    {1005, RTCM_PRIORITY_MEDIUM, 10000, 10000},
    {1006, RTCM_PRIORITY_MEDIUM, 10000, 10000},
};

static const rtcm_profile_rule_t s_profile_rtk_full_rules[] = {
    {1077, RTCM_PRIORITY_HIGH, 2000, 0},
    {1087, RTCM_PRIORITY_HIGH, 2000, 0},
    {1097, RTCM_PRIORITY_HIGH, 2000, 0},
    {1127, RTCM_PRIORITY_HIGH, 2000, 0},
    {1005, RTCM_PRIORITY_MEDIUM, 10000, 10000},
    {1006, RTCM_PRIORITY_MEDIUM, 10000, 10000},
    {1230, RTCM_PRIORITY_MEDIUM, 10000, 10000},
};

static const rtcm_profile_t s_profiles[] = {
    {
        .profile_id = RTCM_PROFILE_RTK_MINIMAL,
        .name = "rtk_minimal",
        .rules = s_profile_rtk_minimal_rules,
        .rule_count = sizeof(s_profile_rtk_minimal_rules) / sizeof(s_profile_rtk_minimal_rules[0]),
        .unknown_action = RTCM_UNKNOWN_ACTION_DROP,
    },
    {
        .profile_id = RTCM_PROFILE_RTK_GPS_ONLY,
        .name = "rtk_gps_only",
        .rules = s_profile_rtk_gps_only_rules,
        .rule_count =
            sizeof(s_profile_rtk_gps_only_rules) / sizeof(s_profile_rtk_gps_only_rules[0]),
        .unknown_action = RTCM_UNKNOWN_ACTION_DROP,
    },
    {
        .profile_id = RTCM_PROFILE_RTK_FULL,
        .name = "rtk_full",
        .rules = s_profile_rtk_full_rules,
        .rule_count = sizeof(s_profile_rtk_full_rules) / sizeof(s_profile_rtk_full_rules[0]),
        .unknown_action = RTCM_UNKNOWN_ACTION_DROP,
    },
    {
        .profile_id = RTCM_PROFILE_CUSTOM,
        .name = "custom",
        .rules = NULL,
        .rule_count = 0,
        .unknown_action = RTCM_UNKNOWN_ACTION_DROP,
    },
};

const rtcm_profile_t* rtcm_profile_get(rtcm_profile_id_t profile_id)
{
  for (size_t i = 0; i < sizeof(s_profiles) / sizeof(s_profiles[0]); i++)
  {
    if (s_profiles[i].profile_id == profile_id)
    {
      return &s_profiles[i];
    }
  }

  return NULL;
}

const char* rtcm_profile_name(rtcm_profile_id_t profile_id)
{
  const rtcm_profile_t* profile = rtcm_profile_get(profile_id);
  return profile != NULL ? profile->name : "unknown";
}

const char* rtcm_priority_name(rtcm_priority_t priority)
{
  switch (priority)
  {
    case RTCM_PRIORITY_HIGH:
      return "HIGH";
    case RTCM_PRIORITY_MEDIUM:
      return "MEDIUM";
    case RTCM_PRIORITY_LOW:
      return "LOW";
    case RTCM_PRIORITY_DROP:
      return "DROP";
    default:
      return "UNKNOWN";
  }
}

const char* rtcm_unknown_action_name(rtcm_unknown_action_t action)
{
  switch (action)
  {
    case RTCM_UNKNOWN_ACTION_DROP:
      return "drop";
    case RTCM_UNKNOWN_ACTION_LOW_PLACEHOLDER:
      return "low_placeholder";
    default:
      return "unknown";
  }
}
