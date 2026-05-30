#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_command_response.hpp"
#include "universal_gnss_driver/receiver_command_transaction.hpp"

namespace
{

using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandKind;
using universal_gnss_driver::ReceiverCommandResponseKind;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
using universal_gnss_driver::ReceiverCommandTransaction;
using universal_gnss_driver::ReceiverCommandTransactionState;

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

ReceiverCommand MakeRuntimeCommand()
{
  ReceiverCommand command;
  command.kind = ReceiverCommandKind::kRawBinary;
  return command;
}

void TestDefaultPendingTransaction(TestContext& ctx)
{
  const ReceiverCommandTransaction transaction{};

  ctx.Expect(transaction.transaction_id == 0u &&
                 transaction.state == ReceiverCommandTransactionState::kPending &&
                 transaction.response.kind == ReceiverCommandResponseKind::kNone,
             "default transaction should start pending with no response");
  ctx.Expect(transaction.attempt_count == 0u &&
                 transaction.max_retries() == 0u &&
                 !transaction.created_timestamp_ns.has_value() &&
                 !transaction.sent_timestamp_ns.has_value() &&
                 !transaction.completed_timestamp_ns.has_value(),
             "default transaction should keep empty timestamps and zero attempts");
}

void TestSentToAckTransition(TestContext& ctx)
{
  ReceiverCommandTransaction transaction;
  transaction.transaction_id = 17u;
  transaction.command = MakeRuntimeCommand();
  transaction.command.retry_policy.max_retries = 2u;
  transaction.created_timestamp_ns = 1000;

  transaction.mark_sent(1100);
  ctx.Expect(transaction.state == ReceiverCommandTransactionState::kSent &&
                 transaction.attempt_count == 1u &&
                 transaction.sent_timestamp_ns == std::optional<std::int64_t>(1100) &&
                 transaction.response.kind == ReceiverCommandResponseKind::kNone,
             "mark_sent should move the transaction into sent state and count an attempt");

  transaction.mark_ack(1200);
  ctx.Expect(transaction.state == ReceiverCommandTransactionState::kAcknowledged &&
                 transaction.response.kind == ReceiverCommandResponseKind::kAck &&
                 transaction.response.timestamp_ns ==
                     std::optional<std::int64_t>(1200) &&
                 transaction.completed_timestamp_ns ==
                     std::optional<std::int64_t>(1200) &&
                 !transaction.can_retry(),
             "mark_ack should complete the transaction successfully");
}

void TestSentToNakTransition(TestContext& ctx)
{
  ReceiverCommandTransaction transaction;
  transaction.command = MakeRuntimeCommand();

  transaction.mark_sent(2000);
  transaction.mark_nak(2100);

  ctx.Expect(transaction.state == ReceiverCommandTransactionState::kRejected &&
                 transaction.attempt_count == 1u &&
                 transaction.response.kind == ReceiverCommandResponseKind::kNak &&
                 transaction.completed_timestamp_ns ==
                     std::optional<std::int64_t>(2100) &&
                 !transaction.can_retry(),
             "mark_nak should reject the transaction without turning it into a retryable timeout");
}

void TestTimeoutRetryPolicy(TestContext& ctx)
{
  ReceiverCommandTransaction transaction;
  transaction.command = MakeRuntimeCommand();
  transaction.command.retry_policy.max_retries = 2u;

  transaction.mark_sent(3000);
  transaction.mark_timeout(3500);

  ctx.Expect(transaction.state == ReceiverCommandTransactionState::kTimedOut &&
                 transaction.response.kind == ReceiverCommandResponseKind::kTimeout &&
                 transaction.completed_timestamp_ns ==
                     std::optional<std::int64_t>(3500) &&
                 transaction.can_retry(),
             "timed out transactions should become retryable when retry budget remains");

  transaction.reset_for_retry();
  ctx.Expect(transaction.state == ReceiverCommandTransactionState::kPending &&
                 transaction.attempt_count == 1u &&
                 transaction.response.kind == ReceiverCommandResponseKind::kNone &&
                 !transaction.sent_timestamp_ns.has_value() &&
                 !transaction.completed_timestamp_ns.has_value(),
             "reset_for_retry should preserve attempt history while clearing response timestamps");
}

void TestMaxRetryEnforcement(TestContext& ctx)
{
  ReceiverCommandTransaction transaction;
  transaction.command = MakeRuntimeCommand();
  transaction.command.retry_policy.max_retries = 1u;

  transaction.mark_sent(4000);
  transaction.mark_timeout(4100);
  ctx.Expect(transaction.can_retry(),
             "first timeout should still allow one configured retry");

  transaction.reset_for_retry();
  transaction.mark_sent(4200);
  transaction.mark_timeout(4300);

  ctx.Expect(transaction.attempt_count == 2u &&
                 !transaction.can_retry() &&
                 transaction.state == ReceiverCommandTransactionState::kTimedOut,
             "retry budget should be exhausted after the configured extra attempt");

  transaction.reset_for_retry();
  ctx.Expect(transaction.state == ReceiverCommandTransactionState::kTimedOut &&
                 transaction.response.kind == ReceiverCommandResponseKind::kTimeout,
             "reset_for_retry should become a no-op once retry budget is exhausted");
}

void TestTransactionKeepsSafetyOutsideStateMachine(TestContext& ctx)
{
  ReceiverCommandTransaction transaction;
  transaction.command.kind = ReceiverCommandKind::kReset;
  transaction.command.safety_level = ReceiverCommandSafetyLevel::kFactoryReset;

  transaction.mark_sent(5000);
  transaction.mark_timeout(5100);

  ctx.Expect(!universal_gnss_driver::HasSafeDispatchApproval(transaction.command) &&
                 transaction.state == ReceiverCommandTransactionState::kTimedOut &&
                 transaction.response.kind == ReceiverCommandResponseKind::kTimeout,
             "transaction state helpers should not replace the separate dispatch safety policy");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestDefaultPendingTransaction(ctx);
  TestSentToAckTransition(ctx);
  TestSentToNakTransition(ctx);
  TestTimeoutRetryPolicy(ctx);
  TestMaxRetryEnforcement(ctx);
  TestTransactionKeepsSafetyOutsideStateMachine(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver command transaction tests passed\n";
  return EXIT_SUCCESS;
}
