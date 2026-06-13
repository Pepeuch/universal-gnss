#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_command_response.hpp"
#include "universal_gnss_driver/receiver_config_application.hpp"
#include "universal_gnss_transport/memory_stream.hpp"

namespace
{

using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandKind;
using universal_gnss_driver::ReceiverCommandResponse;
using universal_gnss_driver::ReceiverCommandResponseKind;
using universal_gnss_driver::ReceiverResponseKind;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
using universal_gnss_driver::ReceiverConfigApplication;
using universal_gnss_driver::ReceiverConfigApplicationConfig;
using universal_gnss_driver::ReceiverConfigApplicationState;
using universal_gnss_transport::MemoryByteSink;

struct TestContext
{
  int failures{0};

  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

ReceiverCommand MakeBinaryCommand(const std::vector<std::uint8_t>& payload,
                                  const std::uint8_t max_retries = 0u,
                                  const std::uint32_t timeout_ms = 500u)
{
  ReceiverCommand command;
  command.kind = ReceiverCommandKind::kRawBinary;
  command.retry_policy.max_retries = max_retries;
  command.retry_policy.timeout_ms = timeout_ms;
  universal_gnss_driver::SetBinaryPayload(command, payload);
  return command;
}

ReceiverCommand MakeTextCommand(const std::string& payload,
                                const std::uint8_t max_retries = 0u,
                                const std::uint32_t timeout_ms = 500u)
{
  ReceiverCommand command;
  command.kind = ReceiverCommandKind::kRawText;
  command.expected_response = universal_gnss_driver::ReceiverResponseKind::kTextPayload;
  command.retry_policy.max_retries = max_retries;
  command.retry_policy.timeout_ms = timeout_ms;
  universal_gnss_driver::SetTextPayload(command, payload);
  return command;
}

ReceiverCommand MakeResetCommand()
{
  ReceiverCommand command;
  command.kind = ReceiverCommandKind::kReset;
  command.expected_response = ReceiverResponseKind::kNone;
  command.safety_level = ReceiverCommandSafetyLevel::kFactoryReset;
  command.explicit_safety_confirmation = true;
  universal_gnss_driver::SetTextPayload(command, "FRESET\r\n");
  return command;
}

ReceiverCommandResponse MakeResponse(const ReceiverCommandResponseKind kind,
                                     const std::optional<std::int64_t> timestamp_ns,
                                     const std::string& message)
{
  ReceiverCommandResponse response;
  response.kind = kind;
  response.timestamp_ns = timestamp_ns;
  response.message = message;
  return response;
}

void TestEmptyCommandListCompletesImmediately(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverConfigApplication application(sink);

  const auto result = application.Start({}, 1000);

  ctx.Expect(result.state == ReceiverConfigApplicationState::kCompleted &&
                 application.state() == ReceiverConfigApplicationState::kCompleted &&
                 application.command_count() == 0u &&
                 application.current_index() == 0u,
             "empty command lists should complete immediately");
  ctx.Expect(application.metrics().commands_total == 0u &&
                 application.metrics().commands_started == 0u &&
                 application.metrics().commands_completed == 0u &&
                 sink.written_bytes().empty(),
             "empty command lists should not dispatch or update command counters");
}

void TestOneCommandSuccess(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverConfigApplication application(sink);

  const auto start = application.Start({MakeTextCommand("MODE ROVER\r\n")}, 2000);
  ctx.Expect(start.state == ReceiverConfigApplicationState::kWaitingForResponse &&
                 start.command_started &&
                 application.current_index() == 0u,
             "starting a one-command application should dispatch the first command");

  const auto applied = application.ApplyResponse(
      MakeResponse(ReceiverCommandResponseKind::kTextOk, 2100, "<OK"));

  ctx.Expect(applied.state == ReceiverConfigApplicationState::kCompleted &&
                 applied.command_finished &&
                 applied.response_applied &&
                 application.state() == ReceiverConfigApplicationState::kCompleted &&
                 application.current_index() == 1u &&
                 application.transaction_engine().completed_transaction().has_value() &&
                 application.transaction_engine().completed_transaction()->response.kind ==
                     ReceiverCommandResponseKind::kTextOk,
             "text_ok should complete a one-command application successfully");
  ctx.Expect(application.metrics().commands_total == 1u &&
                 application.metrics().commands_started == 1u &&
                 application.metrics().commands_completed == 1u &&
                 application.metrics().commands_failed == 0u &&
                 application.metrics().responses_applied == 1u &&
                 sink.written_bytes() ==
                     std::vector<std::uint8_t>(
                         {'M', 'O', 'D', 'E', ' ', 'R', 'O', 'V', 'E', 'R', '\r', '\n'}),
             "successful one-command applications should update metrics and write payload bytes");
}

void TestMultiCommandSuccess(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverConfigApplication application(sink);

  application.Start({MakeBinaryCommand({0x01u}), MakeBinaryCommand({0x02u, 0x03u})}, 3000);
  const auto first = application.ApplyResponse(
      MakeResponse(ReceiverCommandResponseKind::kAck, 3100, "ACK-1"));

  ctx.Expect(first.state == ReceiverConfigApplicationState::kRunning &&
                 first.command_finished &&
                 first.advanced_to_next_command &&
                 application.current_index() == 1u &&
                 application.current_command() != nullptr,
             "successful responses should advance the application to the next command slot");

  const auto step = application.Step(3200);
  ctx.Expect(step.state == ReceiverConfigApplicationState::kWaitingForResponse &&
                 step.command_started &&
                 application.current_index() == 1u,
             "stepping a running application should dispatch the next command");

  const auto second = application.ApplyResponse(
      MakeResponse(ReceiverCommandResponseKind::kAck, 3300, "ACK-2"));

  ctx.Expect(second.state == ReceiverConfigApplicationState::kCompleted &&
                 application.state() == ReceiverConfigApplicationState::kCompleted &&
                 application.current_index() == 2u,
             "the final successful response should complete the application");
  ctx.Expect(application.metrics().commands_total == 2u &&
                 application.metrics().commands_started == 2u &&
                 application.metrics().commands_completed == 2u &&
                 application.metrics().commands_failed == 0u &&
                 sink.written_bytes() ==
                     std::vector<std::uint8_t>({0x01u, 0x02u, 0x03u}),
             "multi-command success should dispatch both commands and update metrics");
}

void TestImmediateAckCommandCompletesWithoutResponse(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverConfigApplication application(sink);

  const auto start = application.Start({MakeResetCommand()}, 3500);

  ctx.Expect(start.state == ReceiverConfigApplicationState::kCompleted &&
                 start.command_started &&
                 start.command_finished &&
                 application.state() == ReceiverConfigApplicationState::kCompleted &&
                 application.current_index() == 1u,
             "commands that expect no response should complete during the initial dispatch step");
  ctx.Expect(application.metrics().commands_total == 1u &&
                 application.metrics().commands_started == 1u &&
                 application.metrics().commands_completed == 1u &&
                 application.metrics().commands_failed == 0u &&
                 application.metrics().responses_applied == 0u &&
                 sink.written_bytes() ==
                     std::vector<std::uint8_t>(
                         {'F', 'R', 'E', 'S', 'E', 'T', '\r', '\n'}),
             "immediate-ack commands should not wait for a response or fabricate response metrics");
}

void TestTextErrorFailsByDefault(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverConfigApplication application(sink);

  application.Start({MakeTextCommand("CONFIG SIGNALGROUP 3 6\r\n")}, 4000);
  const auto result = application.ApplyResponse(
      MakeResponse(ReceiverCommandResponseKind::kTextError,
                   4100,
                   "unsupported command"));

  ctx.Expect(result.state == ReceiverConfigApplicationState::kFailed &&
                 result.command_finished &&
                 result.response_applied &&
                 result.error_message == "unsupported command" &&
                 application.state() == ReceiverConfigApplicationState::kFailed,
             "text_error should fail the application when continue_on_error is disabled");
  ctx.Expect(application.metrics().commands_started == 1u &&
                 application.metrics().commands_completed == 0u &&
                 application.metrics().commands_failed == 1u &&
                 application.metrics().responses_applied == 1u,
             "default response failures should update failed-command metrics");
}

void TestContinueOnErrorAdvancesToNextCommand(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverConfigApplicationConfig config;
  config.continue_on_error = true;
  ReceiverConfigApplication application(sink, config);

  application.Start(
      {MakeTextCommand("BAD COMMAND\r\n"), MakeBinaryCommand({0x55u, 0x66u})},
      5000);
  const auto first = application.ApplyResponse(
      MakeResponse(ReceiverCommandResponseKind::kTextError,
                   5100,
                   "grammar error"));

  ctx.Expect(first.state == ReceiverConfigApplicationState::kRunning &&
                 first.command_finished &&
                 first.advanced_to_next_command &&
                 first.error_message == "grammar error" &&
                 application.current_index() == 1u,
             "continue_on_error should advance after a rejected command response");

  application.Step(5200);
  const auto second = application.ApplyResponse(
      MakeResponse(ReceiverCommandResponseKind::kAck, 5300, "ACK"));

  ctx.Expect(second.state == ReceiverConfigApplicationState::kCompleted &&
                 application.state() == ReceiverConfigApplicationState::kCompleted,
             "continue_on_error should still allow the sequence to complete");
  ctx.Expect(application.metrics().commands_total == 2u &&
                 application.metrics().commands_started == 2u &&
                 application.metrics().commands_completed == 1u &&
                 application.metrics().commands_failed == 1u &&
                 application.metrics().responses_applied == 2u,
             "continue_on_error should preserve both failed and successful command counts");
}

void TestTimeoutThenRetry(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverConfigApplication application(sink);

  application.Start({MakeBinaryCommand({0xA0u, 0xA1u}, 1u, 500u)}, 6000);
  const auto timeout = application.MarkTimeout(6600);

  ctx.Expect(timeout.state == ReceiverConfigApplicationState::kWaitingForResponse &&
                 timeout.retry_dispatched &&
                 application.transaction_engine().current_transaction().has_value() &&
                 application.transaction_engine().current_transaction()->attempt_count == 2u,
             "timeouts with remaining retry budget should redispatch the current command");

  const auto finished = application.ApplyResponse(
      MakeResponse(ReceiverCommandResponseKind::kAck, 6700, "ACK"));

  ctx.Expect(finished.state == ReceiverConfigApplicationState::kCompleted &&
                 application.metrics().commands_retried == 1u &&
                 application.metrics().timeouts_seen == 1u &&
                 application.metrics().commands_failed == 0u &&
                 sink.written_bytes() ==
                     std::vector<std::uint8_t>({0xA0u, 0xA1u, 0xA0u, 0xA1u}),
             "successful retries should preserve completion metrics and duplicate the write");
}

void TestRetryExhaustionFails(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverConfigApplication application(sink);

  application.Start({MakeBinaryCommand({0xB0u}, 1u, 500u)}, 7000);
  application.MarkTimeout(7600);
  const auto exhausted = application.MarkTimeout(8200);

  ctx.Expect(exhausted.state == ReceiverConfigApplicationState::kFailed &&
                 exhausted.command_finished &&
                 application.state() == ReceiverConfigApplicationState::kFailed &&
                 application.transaction_engine().current_transaction().has_value() &&
                 application.transaction_engine().current_transaction()->state ==
                     universal_gnss_driver::ReceiverCommandTransactionState::kTimedOut,
             "retry exhaustion should fail the application after the final timeout");
  ctx.Expect(application.metrics().commands_started == 1u &&
                 application.metrics().commands_failed == 1u &&
                 application.metrics().commands_retried == 1u &&
                 application.metrics().timeouts_seen == 2u,
             "retry exhaustion should update timeout, retry, and failure counters");
}

void TestSafetyRejectionFailsApplication(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverConfigApplication application(sink);

  auto command = MakeBinaryCommand({0xC0u});
  command.safety_level = ReceiverCommandSafetyLevel::kPersistent;

  const auto result = application.Start({command}, 9000);
  ctx.Expect(result.state == ReceiverConfigApplicationState::kFailed &&
                 result.command_finished &&
                 !result.error_message.empty() &&
                 sink.written_bytes().empty(),
             "dispatcher safety rejection should fail the application immediately");
  ctx.Expect(application.metrics().commands_total == 1u &&
                 application.metrics().commands_started == 1u &&
                 application.metrics().commands_failed == 1u &&
                 application.metrics().commands_completed == 0u,
             "safety rejection should count as a failed started command");
}

void TestResetClearsStateAndMetrics(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverConfigApplication application(sink);

  application.Start({MakeBinaryCommand({0xD0u}, 1u, 500u)}, 10000);
  application.MarkTimeout(10600);
  application.Reset();

  ctx.Expect(application.state() == ReceiverConfigApplicationState::kIdle &&
                 application.command_count() == 0u &&
                 application.current_index() == 0u &&
                 application.current_command() == nullptr,
             "reset should clear the loaded command sequence and return to idle");
  ctx.Expect(!application.transaction_engine().current_transaction().has_value() &&
                 !application.transaction_engine().completed_transaction().has_value() &&
                 application.metrics().commands_total == 0u &&
                 application.metrics().commands_started == 0u &&
                 application.metrics().commands_completed == 0u &&
                 application.metrics().commands_failed == 0u &&
                 application.metrics().commands_retried == 0u &&
                 application.metrics().responses_applied == 0u &&
                 application.metrics().timeouts_seen == 0u,
             "reset should clear both application metrics and underlying transaction state");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestEmptyCommandListCompletesImmediately(ctx);
  TestOneCommandSuccess(ctx);
  TestMultiCommandSuccess(ctx);
  TestImmediateAckCommandCompletesWithoutResponse(ctx);
  TestTextErrorFailsByDefault(ctx);
  TestContinueOnErrorAdvancesToNextCommand(ctx);
  TestTimeoutThenRetry(ctx);
  TestRetryExhaustionFails(ctx);
  TestSafetyRejectionFailsApplication(ctx);
  TestResetClearsStateAndMetrics(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver config application tests passed\n";
  return EXIT_SUCCESS;
}
