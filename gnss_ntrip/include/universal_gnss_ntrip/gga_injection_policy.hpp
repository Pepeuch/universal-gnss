#pragma once

#include <cstdint>
#include <optional>

#include "universal_gnss_ntrip/ntrip_config.hpp"

namespace universal_gnss_ntrip
{

enum class GgaSourcePositionRequirement : std::uint8_t
{
  kNone = 0,
  kRequirePositionFix = 1,
};

struct GgaInjectionPolicy
{
  bool enabled{false};
  std::uint32_t interval_s{10u};
  GgaSourcePositionRequirement source_position_requirement{
      GgaSourcePositionRequirement::kRequirePositionFix};
  std::optional<std::int64_t> last_sent_timestamp_ns{};
};

inline GgaInjectionPolicy BuildGgaInjectionPolicy(const NtripConfig& config)
{
  GgaInjectionPolicy policy;
  policy.enabled = config.send_gga;
  policy.interval_s = config.gga_interval_s;
  return policy;
}

inline bool ShouldInjectGga(const GgaInjectionPolicy& policy,
                            const bool has_position_fix,
                            const std::optional<std::int64_t> now_timestamp_ns)
{
  if (!policy.enabled)
  {
    return false;
  }

  if (policy.source_position_requirement == GgaSourcePositionRequirement::kRequirePositionFix &&
      !has_position_fix)
  {
    return false;
  }

  if (!now_timestamp_ns.has_value() || !policy.last_sent_timestamp_ns.has_value())
  {
    return true;
  }

  const std::int64_t interval_ns =
      static_cast<std::int64_t>(policy.interval_s) * 1000000000LL;
  return (*now_timestamp_ns - *policy.last_sent_timestamp_ns) >= interval_ns;
}

inline void MarkGgaInjected(GgaInjectionPolicy& policy, const std::int64_t timestamp_ns)
{
  policy.last_sent_timestamp_ns = timestamp_ns;
}

}  // namespace universal_gnss_ntrip
