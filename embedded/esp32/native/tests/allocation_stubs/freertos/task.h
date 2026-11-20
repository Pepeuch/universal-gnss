// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "freertos/FreeRTOS.h"
constexpr int taskSCHEDULER_NOT_STARTED = 0;
int xTaskGetSchedulerState();
