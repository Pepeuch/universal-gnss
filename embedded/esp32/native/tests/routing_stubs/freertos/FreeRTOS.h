// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
using TickType_t = std::uint32_t;
struct portMUX_TYPE
{
  unsigned unused{};
};
#define portMUX_INITIALIZER_UNLOCKED                                                               \
  {                                                                                                \
  }
void RoutingTestEnter(portMUX_TYPE*);
void RoutingTestExit(portMUX_TYPE*);
#define portENTER_CRITICAL(p) RoutingTestEnter(p)
#define portEXIT_CRITICAL(p) RoutingTestExit(p)
constexpr int pdTRUE = 1, pdPASS = 1, pdFALSE = 0;
constexpr unsigned configTICK_RATE_HZ = 100;
