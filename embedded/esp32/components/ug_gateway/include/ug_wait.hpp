// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstdint>
namespace ug::embedded
{
// FreeRTOS pdMS_TO_TICKS truncates: 5 ms becomes zero at 100 Hz. Ownership
// workers must block at least one tick; never map a finite wait to portMAX_DELAY.
constexpr std::uint32_t FiniteWaitTicks(std::uint32_t ms, std::uint32_t tick_hz)
{
  const auto ticks = (std::uint64_t(ms) * tick_hz + 999) / 1000;
  return ticks == 0 ? 1
                    : (ticks >= UINT32_MAX ? UINT32_MAX - 1 : static_cast<std::uint32_t>(ticks));
}
}  // namespace ug::embedded
