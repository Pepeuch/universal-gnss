// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
constexpr int ESP_ERR_INVALID_STATE = 0x103;
int esp_task_wdt_add(void*);
int esp_task_wdt_delete(void*);
int esp_task_wdt_reset();
