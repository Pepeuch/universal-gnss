// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "freertos/FreeRTOS.h"
using TaskFunction_t = void (*)(void*);
int xTaskCreate(TaskFunction_t, const char*, unsigned, void*, unsigned, void*);
