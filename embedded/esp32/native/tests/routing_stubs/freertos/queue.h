// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "freertos/FreeRTOS.h"
struct StaticQueue_t
{
  std::uint8_t* data{};
  std::size_t depth{}, width{}, head{}, count{};
  bool live{};
};
using QueueHandle_t = StaticQueue_t*;
QueueHandle_t xQueueCreateStatic(std::size_t, std::size_t, std::uint8_t*, StaticQueue_t*);
int xQueueSend(QueueHandle_t, const void*, TickType_t);
int xQueueReceive(QueueHandle_t, void*, TickType_t);
void vQueueDelete(QueueHandle_t);
