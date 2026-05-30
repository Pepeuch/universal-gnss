#include "universal_gnss_ntrip/ntrip_reconnect_policy.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace universal_gnss_ntrip
{

namespace
{

constexpr universal_gnss::GnssTimestampNs kNanosecondsPerMillisecond = 1000000LL;

std::uint32_t ClampDelayMs(const NtripReconnectPolicy& policy, const std::uint64_t delay_ms)
{
  const std::uint64_t capped_delay_ms =
      std::min<std::uint64_t>(delay_ms, static_cast<std::uint64_t>(policy.max_delay_ms));
  return static_cast<std::uint32_t>(capped_delay_ms);
}

std::uint32_t EffectiveInitialDelayMs(const NtripReconnectPolicy& policy)
{
  return ClampDelayMs(policy, policy.initial_delay_ms);
}

std::uint32_t ComputeNextDelayMs(const NtripReconnectPolicy& policy,
                                 const NtripReconnectState& state)
{
  if (state.current_delay_ms == 0u)
  {
    return EffectiveInitialDelayMs(policy);
  }

  const double safe_multiplier = policy.multiplier < 1.0 ? 1.0 : policy.multiplier;
  const double scaled_delay_ms =
      std::ceil(static_cast<double>(state.current_delay_ms) * safe_multiplier);

  const std::uint64_t bounded_delay_ms =
      scaled_delay_ms > static_cast<double>(std::numeric_limits<std::uint32_t>::max())
          ? static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())
          : static_cast<std::uint64_t>(scaled_delay_ms);

  return ClampDelayMs(policy, bounded_delay_ms);
}

std::optional<universal_gnss::GnssTimestampNs> ComputeNextAttemptTimeNs(
    const universal_gnss::GnssTimestampNs now_timestamp_ns,
    const std::uint32_t delay_ms)
{
  if (delay_ms == 0u)
  {
    return now_timestamp_ns;
  }

  if (now_timestamp_ns >=
      std::numeric_limits<universal_gnss::GnssTimestampNs>::max() -
          static_cast<universal_gnss::GnssTimestampNs>(delay_ms) * kNanosecondsPerMillisecond)
  {
    return std::numeric_limits<universal_gnss::GnssTimestampNs>::max();
  }

  return now_timestamp_ns +
         static_cast<universal_gnss::GnssTimestampNs>(delay_ms) * kNanosecondsPerMillisecond;
}

NtripReconnectDecision BuildDecision(const NtripReconnectPolicy& policy,
                                     const NtripReconnectState& state,
                                     const bool scheduled,
                                     const universal_gnss::GnssTimestampNs now_timestamp_ns)
{
  return NtripReconnectDecision{
      scheduled,
      policy.CanAttempt(state),
      policy.ShouldReconnect(state, now_timestamp_ns),
      state.attempt_count,
      state.current_delay_ms,
      state.exhausted,
      state.next_attempt_time_ns};
}

}  // namespace

void NtripReconnectState::Reset()
{
  *this = NtripReconnectState{};
}

NtripReconnectDecision NtripReconnectPolicy::OnFailure(
    NtripReconnectState& state,
    const universal_gnss::GnssTimestampNs now_timestamp_ns) const
{
  state.last_failure_time_ns = now_timestamp_ns;

  if (!enabled)
  {
    state.exhausted = false;
    state.next_attempt_time_ns.reset();
    return BuildDecision(*this, state, false, now_timestamp_ns);
  }

  if (!CanAttempt(state))
  {
    state.exhausted =
        max_attempts.has_value() && state.attempt_count >= *max_attempts;
    state.next_attempt_time_ns.reset();
    return BuildDecision(*this, state, false, now_timestamp_ns);
  }

  state.current_delay_ms = NextDelay(state);
  ++state.attempt_count;
  state.exhausted =
      max_attempts.has_value() && state.attempt_count >= *max_attempts;
  state.next_attempt_time_ns = ComputeNextAttemptTimeNs(now_timestamp_ns, state.current_delay_ms);
  return BuildDecision(*this, state, true, now_timestamp_ns);
}

void NtripReconnectPolicy::OnSuccess(NtripReconnectState& state,
                                     const universal_gnss::GnssTimestampNs now_timestamp_ns) const
{
  if (reset_after_success)
  {
    state.Reset();
    state.last_success_time_ns = now_timestamp_ns;
    return;
  }

  state.last_success_time_ns = now_timestamp_ns;
  state.exhausted = false;
  state.next_attempt_time_ns.reset();
}

bool NtripReconnectPolicy::ShouldReconnect(
    const NtripReconnectState& state,
    const universal_gnss::GnssTimestampNs now_timestamp_ns) const
{
  return enabled &&
         state.next_attempt_time_ns.has_value() &&
         now_timestamp_ns >= *state.next_attempt_time_ns;
}

bool NtripReconnectPolicy::CanAttempt(const NtripReconnectState& state) const
{
  return enabled &&
         !state.exhausted &&
         (!max_attempts.has_value() || state.attempt_count < *max_attempts);
}

std::uint32_t NtripReconnectPolicy::NextDelay(const NtripReconnectState& state) const
{
  return ComputeNextDelayMs(*this, state);
}

}  // namespace universal_gnss_ntrip
