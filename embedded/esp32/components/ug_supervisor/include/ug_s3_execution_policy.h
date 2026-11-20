// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "sdkconfig.h"
#ifndef UG_S3_CPU_PROFILE
#define UG_S3_CPU_PROFILE 0
#endif
#if UG_S3_CPU_PROFILE < 0 || UG_S3_CPU_PROFILE > 2
#error "UG_S3_CPU_PROFILE must be A=0, B=1 or C=2"
#endif
// Boot-time experimental comparators; no priority, stack or SDK affinity change.
// Keep this value equal to the pinned FreeRTOS tskNO_AFFINITY contract.
#define UG_S3_CPU_UNPINNED 0x7fffffff
typedef enum
{
  UG_S3_WORKER_GNSS,
  UG_S3_WORKER_ROUTER,
  UG_S3_WORKER_NETWORK,
  UG_S3_WORKER_WEB,
  UG_S3_WORKER_RADIO
} ug_s3_worker_role_t;
static inline int ug_s3_worker_core(ug_s3_worker_role_t role)
{
#if CONFIG_IDF_TARGET_ESP32S3 && UG_S3_CPU_PROFILE > 0
  if (role == UG_S3_WORKER_GNSS || role == UG_S3_WORKER_ROUTER)
    return 1;
#if UG_S3_CPU_PROFILE == 2
  if (role == UG_S3_WORKER_NETWORK || role == UG_S3_WORKER_WEB || role == UG_S3_WORKER_RADIO)
    return 0;
#endif
#else
  (void)role;
#endif
  return UG_S3_CPU_UNPINNED;
}
