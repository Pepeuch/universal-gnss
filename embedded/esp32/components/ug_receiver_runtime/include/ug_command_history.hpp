// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss_driver/receiver_config_application.hpp"
#include <array>
#include <type_traits>

namespace ug::embedded
{
enum class CommandPurpose : std::uint8_t
{
  kUnspecified,
  kTelemetry,
  kBaseFixed,
  kBaseSurvey,
  kBaseTime,
  kBaseDisable
};
inline const char* CommandPurposeName(CommandPurpose purpose)
{
  switch (purpose)
  {
    case CommandPurpose::kUnspecified:
      return "unspecified";
    case CommandPurpose::kTelemetry:
      return "telemetry";
    case CommandPurpose::kBaseFixed:
      return "base_fixed";
    case CommandPurpose::kBaseSurvey:
      return "base_survey";
    case CommandPurpose::kBaseTime:
      return "base_time";
    case CommandPurpose::kBaseDisable:
      return "base_disable";
  }
  return "invalid";
}
struct CommandEvent
{
  std::uint64_t sequence{}, plan{}, receiver_incarnation{};
  std::int64_t receipt_ns{};
  // This is the application's index AFTER this result, not an inferred wire ID.
  std::size_t application_index{};
  CommandPurpose purpose{};
  universal_gnss_driver::ReceiverConfigApplicationState state{};
  universal_gnss_driver::ReceiverCommandTransactionEngineStepStatus engine_status{};
  bool clock_valid{}, engine_status_present{};
  bool started{}, finished{}, succeeded{}, failed{}, response_applied{}, retry{}, indeterminate{},
      failure_ignored{};
};
constexpr std::size_t kCommandEventCapacity = 16;
struct CommandHistorySnapshot
{
  std::array<CommandEvent, kCommandEventCapacity> events{};  // Oldest to newest.
  std::size_t count{};
  std::uint64_t overwritten{}, rejected{};
  std::uint64_t current_receiver_incarnation{};  // Filled by the receiver snapshot owner.
};
// Single receiver owner only. Volatile diagnostic state; never changes dispatch,
// source freshness, journal or quarantine. Contains no payloads/error strings.
class CommandHistory
{
public:
  void Begin(CommandPurpose purpose)
  {
    if (plan_ != UINT64_MAX)
      ++plan_;
    else
      saturated_ = true;
    purpose_ = purpose;
  }
  void Record(const universal_gnss_driver::ReceiverConfigApplicationResult& result,
              std::uint64_t incarnation,
              std::optional<std::int64_t> receipt_ns)
  {
    // CheckTimeout/Step idleness and unmatched responses cannot flood history.
    if (!result.command_started && !result.command_finished && !result.retry_dispatched)
      return;
    if (!plan_ || saturated_ || sequence_ == UINT64_MAX || (receipt_ns && *receipt_ns < 0))
    {
      if (rejected_ != UINT64_MAX)
        ++rejected_;
      return;
    }
    auto& event = events_[next_];
    event = {};
    event.sequence = ++sequence_;
    event.plan = plan_;
    event.receiver_incarnation = incarnation;
    event.purpose = purpose_;
    event.clock_valid = receipt_ns.has_value();
    event.receipt_ns = receipt_ns.value_or(0);
    event.application_index = result.command_index;
    event.state = result.state;
    event.engine_status_present = result.engine_result.has_value();
    if (result.engine_result)
      event.engine_status = result.engine_result->status;
    event.started = result.command_started;
    event.finished = result.command_finished;
    event.succeeded = result.command_succeeded;
    event.failed = result.command_failed;
    event.response_applied = result.response_applied;
    event.retry = result.retry_dispatched;
    event.indeterminate = result.receiver_state_indeterminate;
    event.failure_ignored = result.failure_ignored;
    next_ = (next_ + 1) % events_.size();
    if (count_ < events_.size())
      ++count_;
    else if (overwritten_ != UINT64_MAX)
      ++overwritten_;
  }
  CommandHistorySnapshot Snapshot() const
  {
    CommandHistorySnapshot out;
    out.count = count_;
    out.overwritten = overwritten_;
    out.rejected = rejected_;
    const auto first = count_ == events_.size() ? next_ : 0;
    for (std::size_t i = 0; i < count_; ++i)
      out.events[i] = events_[(first + i) % events_.size()];
    return out;
  }

private:
  std::array<CommandEvent, kCommandEventCapacity> events_{};
  std::size_t count_{}, next_{};
  std::uint64_t plan_{}, sequence_{}, overwritten_{}, rejected_{};
  CommandPurpose purpose_{};
  bool saturated_{};
};
static_assert(std::is_trivially_copyable_v<CommandEvent>);
static_assert(sizeof(CommandHistorySnapshot) <= 1100);
}  // namespace ug::embedded
