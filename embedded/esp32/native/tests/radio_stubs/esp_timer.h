// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <stdint.h>
// Filter tests explicitly control their supplied observation clock. No device API.
static inline int64_t esp_timer_get_time(void)
{
  return 1000000;
}
