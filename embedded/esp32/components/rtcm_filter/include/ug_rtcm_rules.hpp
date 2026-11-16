// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
namespace ug::embedded
{
// Publisher-owned bounded policy. Clock is caller's monotonic receipt/routing
// domain in ns, never GNSS UTC. No allocation, waiting or retained frame bytes.
class RtcmRules
{
public:
  static constexpr unsigned kMaxRules = 32;
  bool Configure(std::string_view text, std::string& error);
  bool Allows(std::uint16_t type, std::int64_t now_ns) const;
  void Sent(std::uint16_t type, std::int64_t now_ns);

private:
  struct Rule
  {
    std::int64_t last{-1};
    std::uint32_t seconds{};
    std::uint16_t type{};
  };
  std::array<Rule, kMaxRules> rules_{};
  unsigned count_{};
};
static_assert(sizeof(RtcmRules) <= 528);
}  // namespace ug::embedded
