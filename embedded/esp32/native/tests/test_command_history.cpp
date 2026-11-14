// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_receiver_runtime.hpp"
#include "universal_gnss_transport/memory_stream.hpp"
#include <cassert>
#include <cstring>

using namespace ug::embedded;
using namespace universal_gnss_driver;
static ReceiverCommand Query()
{
  ReceiverCommand command;
  command.kind = ReceiverCommandKind::kQuery;
  command.target.vendor = ReceiverVendor::kUnicore;
  command.expected_response = ReceiverResponseKind::kTextPayload;
  command.retry_policy.timeout_ms = 10;
  SetTextPayload(command, "VERSION\r\n");
  return command;
}
static void Feed(Receiver& receiver, const char* bytes, std::int64_t now)
{
  assert(receiver.Feed(reinterpret_cast<const std::uint8_t*>(bytes), std::strlen(bytes), now));
}
int main()
{
  universal_gnss_transport::MemoryByteSink sink;
  Receiver receiver(sink);
  auto query = Query();
  assert(receiver.command_history().count == 0);
  assert(!receiver.Start({}, 0));
  assert(receiver.command_history().count == 0);
  assert(receiver.Start({query, query}, 1, CommandPurpose::kTelemetry));
  assert(!receiver.Start({query}, 2));  // Busy rejection cannot reset the accepted plan's identity.
  auto report = receiver.command_history();
  assert(report.count == 1 && report.events[0].started && !report.events[0].finished);
  assert(report.events[0].plan == 1 && report.events[0].application_index == 0);
  assert(report.events[0].receiver_incarnation == 1 && report.events[0].receipt_ns == 1);
  assert(report.events[0].purpose == CommandPurpose::kTelemetry);
  Feed(receiver, "<OK\r\n<OK\r\n", 3);
  report = receiver.command_history();
  assert(report.count == 2 && report.events[1].succeeded && report.events[1].response_applied);
  assert(report.events[1].application_index == 1);  // Post-result application index.
  assert(report.events[1].state == ReceiverConfigApplicationState::kRunning);
  receiver.Step(4);
  assert(receiver.command_history().count == 3);
  Feed(receiver, "<OK\r\n", 5);
  report = receiver.command_history();
  assert(report.count == 4 && report.events[3].succeeded && report.events[3].finished);
  assert(report.events[3].state == ReceiverConfigApplicationState::kCompleted);
  assert(report.events[3].engine_status ==
         ReceiverCommandTransactionEngineStepStatus::kAcknowledged);
  for (unsigned i = 0; i < 100; ++i)
    receiver.Step(6);
  assert(receiver.command_history().count == 4);  // Completed ticks cannot renew result freshness.
  assert(receiver.command_history().events[3].receipt_ns == 5);
  receiver.InvalidateObservations();
  assert(receiver.command_history().count == 4);
  assert(receiver.Start({query}, 7, CommandPurpose::kBaseDisable));
  receiver.Step(10000007);
  report = receiver.command_history();
  assert(report.count == 6 && report.events[5].failed && report.events[5].indeterminate);
  assert(report.events[5].purpose == CommandPurpose::kBaseDisable && report.events[5].plan == 2);
  assert(report.events[5].receiver_incarnation == 2 && report.events[5].clock_valid);
  assert(report.events[5].engine_status == ReceiverCommandTransactionEngineStepStatus::kTimedOut);
  receiver.InvalidateObservations();
  Feed(receiver, "<OK\r\n", 10000008);
  assert(receiver.indeterminate() && !receiver.Start({query}, 10000009));
  assert(receiver.command_history().count == 6);
  assert(receiver.command_history()
             .events[5]
             .indeterminate);  // Late ACK/reset never rewrites history.

  Receiver interrupted(sink);
  assert(interrupted.Start({query}, 1));
  interrupted.InvalidateObservations();
  report = interrupted.command_history();
  assert(report.count == 2 && report.events[1].indeterminate);
  assert(
      !report.events[1].clock_valid);  // No invented timestamp for an un-timestamped invalidation.
  assert(report.events[1].receiver_incarnation == 1);
  assert(interrupted.incarnation() == 2);
  assert(report.current_receiver_incarnation == 2 && report.events[1].receiver_incarnation == 1);

  Receiver rejected(sink);
  assert(rejected.Start({query}, 1));
  Feed(rejected, "Unsupported command\r\n", 2);
  report = rejected.command_history();
  assert(report.count == 2 && report.events[1].failed && !report.events[1].indeterminate);
  assert(report.events[1].engine_status == ReceiverCommandTransactionEngineStepStatus::kRejected);
  assert(rejected.Start({query}, 3));
  assert(rejected.command_history().events[2].plan == 2);

  struct PartialSink : universal_gnss_transport::ByteSink
  {
    bool IsOpen() const override
    {
      return true;
    }
    void Close() override
    {
    }
    universal_gnss_transport::WriteResult Write(const std::uint8_t*, std::size_t) override
    {
      return {1,
              universal_gnss_transport::TransportStatus::kError,
              universal_gnss_transport::TransportError::kWriteFailure};
    }
  } partial_sink;
  Receiver partial(partial_sink);
  assert(!partial.Start({query}, 1));
  report = partial.command_history();
  assert(report.count == 1 && report.events[0].started && report.events[0].failed);
  assert(report.events[0].indeterminate && partial.indeterminate());

  Receiver no_response(sink);
  auto unchecked = query;
  unchecked.expected_response = ReceiverResponseKind::kNone;
  assert(no_response.Start({unchecked}, 1));
  report = no_response.command_history();
  assert(report.count == 1 && report.events[0].started && report.events[0].finished);
  assert(report.events[0].succeeded && !report.events[0].response_applied);

  Receiver wrapped(sink);
  std::int64_t now = 0;
  for (unsigned i = 0; i < 30; ++i)
  {
    assert(wrapped.Start({query}, ++now));
    Feed(wrapped, "<OK\r\n", ++now);
  }
  report = wrapped.command_history();
  assert(report.count == kCommandEventCapacity && report.overwritten == 60 - kCommandEventCapacity);
  assert(report.events[0].sequence == 45 && report.events[15].sequence == 60);
  assert(report.events[15].plan == 30 && report.events[15].receipt_ns == now);
  for (std::size_t i = 1; i < report.count; ++i)
    assert(report.events[i].sequence == report.events[i - 1].sequence + 1);
  const auto retained = wrapped.command_history();
  wrapped.InvalidateObservations();
  assert(retained.events[0].sequence == wrapped.command_history().events[0].sequence);

  CommandHistory ring;
  ReceiverConfigApplicationResult result;
  result.command_started = true;
  ring.Record(result, 1, 0);  // No plan: fail diagnostic insertion only.
  assert(ring.Snapshot().rejected == 1 && ring.Snapshot().count == 0);
  ring.Begin(CommandPurpose::kBaseFixed);
  ring.Record(result, 1, -1);
  assert(ring.Snapshot().rejected == 2);
  ring.Record(result, 1, std::nullopt);
  assert(!ring.Snapshot().events[0].clock_valid);
  assert(std::strcmp(CommandPurposeName(CommandPurpose::kBaseSurvey), "base_survey") == 0);
  assert(std::strcmp(CommandPurposeName(CommandPurpose::kBaseTime), "base_time") == 0);
}
