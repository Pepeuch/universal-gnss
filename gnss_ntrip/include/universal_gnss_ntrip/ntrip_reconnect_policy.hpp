#pragma once

#include <cstdint>
#include <optional>

#include "universal_gnss/gnss_types.hpp"

namespace universal_gnss_ntrip
{

struct NtripReconnectState
{
  std::uint32_t attempt_count{0u};
  std::optional<universal_gnss::GnssTimestampNs> next_attempt_time_ns{};
  std::optional<universal_gnss::GnssTimestampNs> last_failure_time_ns{};
  std::optional<universal_gnss::GnssTimestampNs> last_success_time_ns{};
  std::uint32_t current_delay_ms{0u};
  bool exhausted{false};

  void Reset();
};

struct NtripReconnectDecision
{
  bool scheduled{false};
  bool can_attempt{false};
  bool should_reconnect{false};
  std::uint32_t attempt_count{0u};
  std::uint32_t current_delay_ms{0u};
  bool exhausted{false};
  std::optional<universal_gnss::GnssTimestampNs> next_attempt_time_ns{};
};

struct NtripReconnectPolicy
{
  bool enabled{true};
  std::uint32_t initial_delay_ms{1000u};
  std::uint32_t max_delay_ms{30000u};
  double multiplier{2.0};
  bool jitter_enabled{false};
  std::optional<std::uint32_t> max_attempts{};
  bool reset_after_success{true};

  NtripReconnectDecision OnFailure(
      NtripReconnectState& state,
      universal_gnss::GnssTimestampNs now_timestamp_ns) const;
  void OnSuccess(NtripReconnectState& state,
                 universal_gnss::GnssTimestampNs now_timestamp_ns) const;
  bool ShouldReconnect(const NtripReconnectState& state,
                       universal_gnss::GnssTimestampNs now_timestamp_ns) const;
  bool CanAttempt(const NtripReconnectState& state) const;
  std::uint32_t NextDelay(const NtripReconnectState& state) const;
};

using NtripReconnectBackoff = NtripReconnectPolicy;

}  // namespace universal_gnss_ntrip
