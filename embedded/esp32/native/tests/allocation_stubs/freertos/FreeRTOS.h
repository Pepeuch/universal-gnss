// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <mutex>
bool xPortInIsrContext();
struct portMUX_TYPE
{
  std::mutex mutex;
};
#define portMUX_INITIALIZER_UNLOCKED                                                               \
  {                                                                                                \
  }
void AllocationCriticalEnter(portMUX_TYPE*);
void AllocationCriticalExit(portMUX_TYPE*);
#define portENTER_CRITICAL(p) AllocationCriticalEnter(p)
#define portEXIT_CRITICAL(p) AllocationCriticalExit(p)
