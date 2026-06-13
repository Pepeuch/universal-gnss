#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_command_response.hpp"
#include "universal_gnss_driver/receiver_command_transaction_engine.hpp"
#include "universal_gnss_protocols/ubx_cfg_builder.hpp"
#include "universal_gnss_transport/memory_stream.hpp"
#include "universal_gnss_transport/transport_error.hpp"

namespace
{

using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandKind;
using universal_gnss_driver::ReceiverCommandResponse;
using universal_gnss_driver::ReceiverCommandResponseKind;
using universal_gnss_driver::ReceiverResponseKind;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
using universal_gnss_driver::ReceiverCommandTransactionEngine;
using universal_gnss_driver::ReceiverCommandTransactionEngineStepStatus;
using universal_gnss_driver::ReceiverCommandTransactionState;
using universal_gnss_driver::ReceiverCommandResponseMatchMetadata;
using universal_gnss_driver::UbxMessageIdentity;
using universal_gnss_transport::MemoryByteSink;
using universal_gnss_transport::TransportError;

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

ReceiverCommand MakeBinaryRuntimeCommand(const std::vector<std::uint8_t>& payload,
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

ReceiverCommand MakeUbxCfgCommand(const std::uint8_t max_retries = 0u,
                                  const std::uint32_t timeout_ms = 500u)
{
  const auto builder_result =
      universal_gnss_protocols::BuildUart1BaudrateFrame(115200u);
  if (builder_result.status != universal_gnss_protocols::UbxCfgBuilderStatus::kOk)
  {
    std::cerr << "FAILED: test setup could not build a UBX CFG frame\n";
    std::exit(EXIT_FAILURE);
  }

  return MakeBinaryRuntimeCommand(builder_result.frame, max_retries, timeout_ms);
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

void TestSuccessfulDispatchCreatesCurrentTransaction(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandTransactionEngine engine(sink);

  const auto result = engine.StartTransaction(
      MakeBinaryRuntimeCommand({0xAAu, 0x55u, 0x10u}), 1000);

  ctx.Expect(result.status == ReceiverCommandTransactionEngineStepStatus::kDispatched &&
                 result.dispatch_result.has_value(),
             "successful dispatch should report a dispatched engine step");
  ctx.Expect(engine.current_transaction().has_value() &&
                 engine.current_transaction()->transaction_id == 1u &&
                 engine.current_transaction()->state == ReceiverCommandTransactionState::kSent &&
                 engine.current_transaction()->attempt_count == 1u &&
                 engine.current_transaction()->created_timestamp_ns ==
                     std::optional<std::int64_t>(1000) &&
                 engine.current_transaction()->sent_timestamp_ns ==
                     std::optional<std::int64_t>(1000),
             "successful dispatch should keep a current sent transaction with timestamps");
  ctx.Expect(sink.written_bytes() == std::vector<std::uint8_t>({0xAAu, 0x55u, 0x10u}) &&
                 engine.metrics().transactions_created == 1u &&
                 engine.metrics().commands_dispatched == 1u &&
                 engine.metrics().dispatch_failures == 0u,
             "successful dispatch should write bytes and update engine metrics");
}

void TestAckResponseMarksTransactionAcknowledged(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandTransactionEngine engine(sink);
  engine.StartTransaction(MakeBinaryRuntimeCommand({0x42u}), 1100);

  ReceiverCommandResponse response;
  response.kind = ReceiverCommandResponseKind::kAck;
  response.timestamp_ns = 1200;
  response.message = "ACK";
  const auto result = engine.ApplyResponse(response);

  ctx.Expect(result.status == ReceiverCommandTransactionEngineStepStatus::kAcknowledged &&
                 result.response_matched,
             "ACK responses should be accepted by a sent transaction");
  ctx.Expect(!engine.current_transaction().has_value() &&
                 engine.completed_transaction().has_value() &&
                 engine.completed_transaction()->state ==
                     ReceiverCommandTransactionState::kAcknowledged &&
                 engine.completed_transaction()->response.kind ==
                     ReceiverCommandResponseKind::kAck &&
                 engine.completed_transaction()->response.message == "ACK",
             "accepted ACK responses should complete the transaction successfully");
  ctx.Expect(engine.metrics().responses_accepted == 1u &&
                 engine.metrics().transactions_acknowledged == 1u &&
                 engine.metrics().responses_unmatched == 0u,
             "ACK acceptance should update response and acknowledgement counters");
}

void TestNoResponseCommandsAcknowledgeImmediately(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandTransactionEngine engine(sink);

  const auto result = engine.StartTransaction(MakeResetCommand(), 1150);

  ctx.Expect(result.status == ReceiverCommandTransactionEngineStepStatus::kAcknowledged &&
                 result.dispatch_result.has_value(),
             "no-response commands should acknowledge immediately after dispatch");
  ctx.Expect(!engine.current_transaction().has_value() &&
                 engine.completed_transaction().has_value() &&
                 engine.completed_transaction()->state ==
                     ReceiverCommandTransactionState::kAcknowledged &&
                 engine.completed_transaction()->sent_timestamp_ns ==
                     std::optional<std::int64_t>(1150),
             "immediate-ack commands should complete without leaving a current transaction pending");
  ctx.Expect(engine.metrics().transactions_created == 1u &&
                 engine.metrics().transactions_acknowledged == 1u &&
                 sink.written_bytes() ==
                     std::vector<std::uint8_t>(
                         {'F', 'R', 'E', 'S', 'E', 'T', '\r', '\n'}),
             "immediate-ack commands should still write bytes and update acknowledgement metrics");
}

void TestNakResponseMarksTransactionRejected(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandTransactionEngine engine(sink);
  engine.StartTransaction(MakeBinaryRuntimeCommand({0x43u}), 2100);

  ReceiverCommandResponse response;
  response.kind = ReceiverCommandResponseKind::kNak;
  response.timestamp_ns = 2200;
  response.message = "NAK";
  const auto result = engine.ApplyResponse(response);

  ctx.Expect(result.status == ReceiverCommandTransactionEngineStepStatus::kRejected &&
                 result.response_matched,
             "NAK responses should be accepted by a sent transaction");
  ctx.Expect(!engine.current_transaction().has_value() &&
                 engine.completed_transaction().has_value() &&
                 engine.completed_transaction()->state ==
                     ReceiverCommandTransactionState::kRejected &&
                 engine.completed_transaction()->response.kind ==
                     ReceiverCommandResponseKind::kNak,
             "accepted NAK responses should complete the transaction as rejected");
  ctx.Expect(engine.metrics().responses_accepted == 1u &&
                 engine.metrics().transactions_rejected == 1u,
             "NAK acceptance should update rejection counters");
}

void TestUnmatchedResponseIncrementsCounter(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandTransactionEngine engine(sink);
  engine.StartTransaction(MakeUbxCfgCommand(), 3000);

  ReceiverCommandResponse response;
  response.kind = ReceiverCommandResponseKind::kAck;
  response.timestamp_ns = 3100;

  ReceiverCommandResponseMatchMetadata match_metadata;
  match_metadata.ubx_target = UbxMessageIdentity{0x06u, 0x8Bu};

  const auto result = engine.ApplyResponse(response, match_metadata);

  ctx.Expect(result.status == ReceiverCommandTransactionEngineStepStatus::kResponseUnmatched &&
                 !result.response_matched,
             "responses whose metadata does not match the current command should be rejected");
  ctx.Expect(engine.current_transaction().has_value() &&
                 engine.current_transaction()->state == ReceiverCommandTransactionState::kSent &&
                 engine.metrics().responses_unmatched == 1u &&
                 engine.metrics().responses_accepted == 0u,
             "unmatched responses should keep the current transaction pending and update metrics");
}

void TestTimeoutMarksTransactionTimedOut(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandTransactionEngine engine(sink);
  engine.StartTransaction(MakeBinaryRuntimeCommand({0x44u}, 1u, 500u), 4000);

  const auto early = engine.CheckTimeout(500003999LL);
  ctx.Expect(early.status == ReceiverCommandTransactionEngineStepStatus::kNotTimedOut,
             "timeout checks before the configured deadline should not fire");

  const auto timed_out = engine.CheckTimeout(500004000LL);
  ctx.Expect(timed_out.status == ReceiverCommandTransactionEngineStepStatus::kTimedOut,
             "timeout checks at or after the configured deadline should time out the transaction");
  ctx.Expect(engine.current_transaction().has_value() &&
                 engine.current_transaction()->state ==
                     ReceiverCommandTransactionState::kTimedOut &&
                 engine.current_transaction()->response.kind ==
                     ReceiverCommandResponseKind::kTimeout &&
                 engine.metrics().transactions_timed_out == 1u,
             "timed out transactions should remain current for possible manual retry");
}

void TestRetryAllowedWithinRetryBudget(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandTransactionEngine engine(sink);
  const auto command = MakeBinaryRuntimeCommand({0x45u, 0x46u}, 1u);
  engine.StartTransaction(command, 5000);
  engine.MarkTimeout(5600);

  const auto retry = engine.RetryPending(5700);
  ctx.Expect(retry.status == ReceiverCommandTransactionEngineStepStatus::kRetryDispatched &&
                 retry.dispatch_result.has_value(),
             "retryable timed out transactions should be redispatched explicitly");
  ctx.Expect(engine.current_transaction().has_value() &&
                 engine.current_transaction()->state == ReceiverCommandTransactionState::kSent &&
                 engine.current_transaction()->attempt_count == 2u &&
                 engine.current_transaction()->sent_timestamp_ns ==
                     std::optional<std::int64_t>(5700),
             "manual retry should increment attempts and return the transaction to sent state");
  ctx.Expect(sink.written_bytes() ==
                 std::vector<std::uint8_t>({0x45u, 0x46u, 0x45u, 0x46u}) &&
                 engine.metrics().commands_dispatched == 2u,
             "manual retry should write the command bytes again and update dispatch counters");
}

void TestRetryStopsAfterMaxRetries(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandTransactionEngine engine(sink);
  engine.StartTransaction(MakeBinaryRuntimeCommand({0x47u}, 1u), 6000);
  engine.MarkTimeout(6500);
  engine.RetryPending(6600);
  engine.MarkTimeout(7100);

  const auto retry = engine.RetryPending(7200);
  ctx.Expect(retry.status == ReceiverCommandTransactionEngineStepStatus::kRetryUnavailable,
             "retry should stop once the configured retry budget is exhausted");
  ctx.Expect(engine.current_transaction().has_value() &&
                 engine.current_transaction()->state ==
                     ReceiverCommandTransactionState::kTimedOut &&
                 engine.current_transaction()->attempt_count == 2u &&
                 !engine.current_transaction()->can_retry(),
             "exhausted retries should leave the transaction timed out and non-retryable");
}

void TestDispatchFailureMarksTransactionFailed(TestContext& ctx)
{
  MemoryByteSink sink;
  sink.InjectNextWriteError(TransportError::kWriteFailure);
  ReceiverCommandTransactionEngine engine(sink);

  const auto result = engine.StartTransaction(MakeBinaryRuntimeCommand({0x48u}), 8000);
  ctx.Expect(result.status == ReceiverCommandTransactionEngineStepStatus::kDispatchFailed &&
                 result.dispatch_result.has_value() &&
                 result.dispatch_result->transport_error == TransportError::kWriteFailure,
             "dispatch failures should be surfaced through the engine");
  ctx.Expect(!engine.current_transaction().has_value() &&
                 engine.completed_transaction().has_value() &&
                 engine.completed_transaction()->state ==
                     ReceiverCommandTransactionState::kFailed &&
                 engine.completed_transaction()->response.message == "transport write failed",
             "dispatch failures should complete the transaction as failed");
  ctx.Expect(engine.metrics().dispatch_failures == 1u &&
                 engine.metrics().commands_dispatched == 0u,
             "dispatch failures should increment engine failure counters without sent counts");
}

void TestSafetyRejectionPreventsDispatch(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandTransactionEngine engine(sink);

  auto command = MakeBinaryRuntimeCommand({0x49u});
  command.safety_level = ReceiverCommandSafetyLevel::kPersistent;
  const auto result = engine.StartTransaction(command, 9000);

  ctx.Expect(result.status == ReceiverCommandTransactionEngineStepStatus::kDispatchFailed &&
                 result.dispatch_result.has_value(),
             "unsafe commands should be rejected by the engine through dispatcher failure");
  ctx.Expect(sink.written_bytes().empty() &&
                 !engine.current_transaction().has_value() &&
                 engine.completed_transaction().has_value() &&
                 engine.completed_transaction()->state ==
                     ReceiverCommandTransactionState::kFailed &&
                 engine.completed_transaction()->attempt_count == 0u,
             "safety rejection should prevent any bytes from being dispatched");
}

void TestResetClearsStateAndMetrics(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandTransactionEngine engine(sink);
  engine.StartTransaction(MakeBinaryRuntimeCommand({0x50u}), 10000);
  engine.MarkTimeout(10500);
  engine.Reset();

  ctx.Expect(!engine.current_transaction().has_value() &&
                 !engine.completed_transaction().has_value(),
             "engine reset should clear both current and completed transaction state");
  ctx.Expect(engine.metrics().transactions_created == 0u &&
                 engine.metrics().commands_dispatched == 0u &&
                 engine.metrics().responses_accepted == 0u &&
                 engine.metrics().responses_unmatched == 0u &&
                 engine.metrics().transactions_timed_out == 0u &&
                 engine.metrics().dispatch_failures == 0u,
             "engine reset should clear metrics");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestSuccessfulDispatchCreatesCurrentTransaction(ctx);
  TestAckResponseMarksTransactionAcknowledged(ctx);
  TestNoResponseCommandsAcknowledgeImmediately(ctx);
  TestNakResponseMarksTransactionRejected(ctx);
  TestUnmatchedResponseIncrementsCounter(ctx);
  TestTimeoutMarksTransactionTimedOut(ctx);
  TestRetryAllowedWithinRetryBudget(ctx);
  TestRetryStopsAfterMaxRetries(ctx);
  TestDispatchFailureMarksTransactionFailed(ctx);
  TestSafetyRejectionPreventsDispatch(ctx);
  TestResetClearsStateAndMetrics(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver command transaction engine tests passed\n";
  return EXIT_SUCCESS;
}
