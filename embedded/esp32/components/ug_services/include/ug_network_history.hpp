// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_ntrip_trace.hpp"
#include "ug_service_health.hpp"
#include "universal_gnss_ntrip/ntrip_metrics.hpp"
namespace ug::embedded
{
struct NetworkTraceEvent
{
  std::uint64_t sequence{}, configuration_revision{}, link_generation{}, attempt{};
  std::int64_t receipt_ns{};
  ServiceId service{};
  NtripTraceKind kind{};
  NtripTraceError error{};
  std::uint8_t client_error{};  // NtripClientError, only when error == kClient.
};
constexpr std::size_t kNetworkTraceCapacity = 16;
struct NetworkTraceSnapshot
{
  std::array<NetworkTraceEvent, kNetworkTraceCapacity> events{};
  std::size_t count{};
  std::uint64_t overwritten{}, rejected{}, coalesced{};
};
// Caller serializes append/snapshot. Fixed, boot-local, non-destructive history;
// deliberately stores no endpoint, mountpoint, credentials, position or raw reply.
class NetworkTraceRing
{
public:
  bool Append(NetworkTraceEvent event)
  {
    const bool owner =
        event.service == ServiceId::kNtripInput ||
        (event.service >= ServiceId::kPublisher0 && event.service <= ServiceId::kPublisher4);
    if (!owner || event.kind > NtripTraceKind::kStopped ||
        event.error > NtripTraceError::kCancelled || event.receipt_ns < 0 ||
        (event.error != NtripTraceError::kClient && event.client_error != 0) ||
        event.client_error >
            static_cast<std::uint8_t>(universal_gnss_ntrip::NtripClientError::kUnknown) ||
        ((event.kind == NtripTraceKind::kFailure) != (event.error != NtripTraceError::kNone)) ||
        (event.error == NtripTraceError::kClient && !event.client_error) || sequence_ == UINT64_MAX)
    {
      if (rejected_ != UINT64_MAX)
        ++rejected_;
      return false;
    }
    if (count_ && event.kind == NtripTraceKind::kFailure)
    {
      const auto& last = events_[(next_ + events_.size() - 1) % events_.size()];
      if (event.service == last.service && event.kind == last.kind && event.error == last.error &&
          event.client_error == last.client_error && event.attempt == last.attempt &&
          event.configuration_revision == last.configuration_revision &&
          event.link_generation == last.link_generation)
      {
        if (coalesced_ != UINT64_MAX)
          ++coalesced_;
        return true;  // Keep the first receipt, never refresh it with polling errors.
      }
    }
    event.sequence = ++sequence_;
    events_[next_] = event;
    next_ = (next_ + 1) % events_.size();
    if (count_ < events_.size())
      ++count_;
    else if (overwritten_ != UINT64_MAX)
      ++overwritten_;
    return true;
  }
  NetworkTraceSnapshot Snapshot() const
  {
    NetworkTraceSnapshot out;
    out.count = count_;
    out.overwritten = overwritten_;
    out.rejected = rejected_;
    out.coalesced = coalesced_;
    const auto first = count_ == events_.size() ? next_ : 0;
    for (std::size_t i = 0; i < count_; ++i)
      out.events[i] = events_[(first + i) % events_.size()];
    return out;
  }

private:
  std::array<NetworkTraceEvent, kNetworkTraceCapacity> events_{};
  std::size_t count_{}, next_{};
  std::uint64_t sequence_{}, overwritten_{}, rejected_{}, coalesced_{};
};
}  // namespace ug::embedded
