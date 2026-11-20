// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <atomic>
namespace ug::embedded
{
// Fixed control storage. Payloads are never accessed by two executors together.
// A cancellation request does not return ownership: only the helper can Return.
class ExclusiveJob
{
public:
  enum class State
  {
    kOwner,
    kRequested,
    kExecuting,
    kReturned
  };
  bool Request()
  {
    return Change(State::kOwner, State::kRequested);
  }
  bool Take()
  {
    return Change(State::kRequested, State::kExecuting);
  }
  bool CancelPending()
  {
    return Change(State::kRequested, State::kOwner);
  }
  bool Return()
  {
    return Change(State::kExecuting, State::kReturned);
  }
  bool Reclaim()
  {
    return Change(State::kReturned, State::kOwner);
  }
  State state() const
  {
    return state_.load(std::memory_order_acquire);
  }

private:
  bool Change(State from, State to)
  {
    return state_.compare_exchange_strong(from, to, std::memory_order_acq_rel);
  }
  std::atomic<State> state_{State::kOwner};
};
}  // namespace ug::embedded
