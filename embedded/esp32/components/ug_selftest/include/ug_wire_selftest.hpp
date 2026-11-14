// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstdint>
#include <string_view>

namespace ug::embedded
{
constexpr unsigned kWireSelfTestCount = 6;
constexpr std::uint32_t kWireSelfTestMask = (1u << kWireSelfTestCount) - 1;
struct WireSelfTestReport
{
  std::uint32_t passed{}, failed{};
  bool ok() const
  {
    return passed == kWireSelfTestMask && failed == 0;
  }
};
const char* WireSelfTestName(unsigned index);
bool DecodeWireSelfTest(std::string_view body);
// Finite private codec state and fixed test buffers only. No transport I/O,
// production routing, clock read, task, configuration or GNSS receiver access.
// The reused codec may emit its ordinary SDK diagnostic messages.
WireSelfTestReport RunWireSelfTest();
}  // namespace ug::embedded
