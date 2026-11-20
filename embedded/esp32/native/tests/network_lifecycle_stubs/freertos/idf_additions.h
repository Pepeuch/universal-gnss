// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "freertos/task.h"
int xTaskCreateWithCaps(TaskFunction_t, const char*, unsigned, void*, unsigned, void*, unsigned);
