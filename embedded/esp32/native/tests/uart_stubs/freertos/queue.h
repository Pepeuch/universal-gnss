// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
using QueueHandle_t = void*;
unsigned uxQueueMessagesWaiting(QueueHandle_t);
int xQueueReceive(QueueHandle_t, void*, unsigned);
