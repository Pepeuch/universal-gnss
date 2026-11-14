// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_service_health.hpp"
namespace ug::embedded
{
enum class ServiceEventKind : std::uint8_t
{
  kState,
  kWatchdog,
  kWorkerExit
};
inline const char* ServiceEventName(ServiceEventKind kind)
{
  switch (kind)
  {
    case ServiceEventKind::kState:
      return "state";
    case ServiceEventKind::kWatchdog:
      return "watchdog";
    case ServiceEventKind::kWorkerExit:
      return "worker_exit";
  }
  return "invalid";
}
struct ServiceEvent
{
  std::uint64_t sequence{};
  std::int64_t receipt_ns{};
  std::int32_t error{};
  ServiceId service{};
  ServiceState state{};
  ServiceEventKind kind{};
};
constexpr std::size_t kServiceEventCapacity = 32;
struct ServiceEventSnapshot
{
  std::array<ServiceEvent, kServiceEventCapacity> events{};  // Oldest to newest.
  std::size_t count{};
  std::uint64_t overwritten{}, rejected{};
};
// Volatile boot-local history. Typed values only; never an SDK printf/log hook.
// Caller serializes access; no dynamic allocation or references to input memory.
class ServiceEventRing
{
public:
  bool Append(ServiceId service,
              ServiceState state,
              ServiceEventKind kind,
              std::int32_t error,
              std::int64_t receipt_ns)
  {
    if (!ValidService(service) || state > ServiceState::kBlocked ||
        kind > ServiceEventKind::kWorkerExit || receipt_ns < 0 || sequence_ == UINT64_MAX)
    {
      if (rejected_ != UINT64_MAX)
        ++rejected_;
      return false;
    }
    events_[next_] = {++sequence_, receipt_ns, error, service, state, kind};
    next_ = (next_ + 1) % events_.size();
    if (count_ < events_.size())
      ++count_;
    else if (overwritten_ != UINT64_MAX)
      ++overwritten_;
    return true;
  }
  ServiceEventSnapshot Snapshot() const
  {
    ServiceEventSnapshot out;
    out.count = count_;
    out.overwritten = overwritten_;
    out.rejected = rejected_;
    const auto first = count_ == events_.size() ? next_ : 0;
    for (std::size_t i = 0; i < count_; ++i)
      out.events[i] = events_[(first + i) % events_.size()];
    return out;
  }

private:
  std::array<ServiceEvent, kServiceEventCapacity> events_{};
  std::size_t count_{}, next_{};
  std::uint64_t sequence_{}, overwritten_{}, rejected_{};
};
}  // namespace ug::embedded
