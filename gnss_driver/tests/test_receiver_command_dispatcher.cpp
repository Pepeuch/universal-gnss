#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_command_dispatcher.hpp"
#include "universal_gnss_transport/memory_stream.hpp"

namespace
{

using universal_gnss_driver::DispatchStatus;
using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandDispatcher;
using universal_gnss_driver::ReceiverCommandDispatcherConfig;
using universal_gnss_driver::ReceiverCommandKind;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
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

ReceiverCommand MakeBinaryRuntimeCommand(const std::vector<std::uint8_t>& payload)
{
  ReceiverCommand command;
  command.kind = ReceiverCommandKind::kRawBinary;
  universal_gnss_driver::SetBinaryPayload(command, payload);
  return command;
}

ReceiverCommand MakeTextRuntimeCommand(const std::string& payload)
{
  ReceiverCommand command;
  command.kind = ReceiverCommandKind::kRawText;
  universal_gnss_driver::SetTextPayload(command, payload);
  return command;
}

void TestRuntimeCommandDispatchSucceeds(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandDispatcher dispatcher(sink);
  const ReceiverCommand command = MakeBinaryRuntimeCommand({0xAAu, 0x55u, 0x10u});

  const auto result = dispatcher.Dispatch(command);
  ctx.Expect(result.status == DispatchStatus::kSent &&
                 result.bytes_written == 3u &&
                 sink.written_bytes() == std::vector<std::uint8_t>({0xAAu, 0x55u, 0x10u}),
             "runtime binary commands should dispatch prepared bytes successfully");
  ctx.Expect(dispatcher.metrics().commands_attempted == 1u &&
                 dispatcher.metrics().commands_sent == 1u &&
                 dispatcher.metrics().bytes_written == 3u &&
                 dispatcher.metrics().write_errors == 0u,
             "successful runtime dispatch should update dispatcher metrics");
}

void TestPersistentSafetyPolicy(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandDispatcher dispatcher(sink);

  ReceiverCommand persistent = MakeBinaryRuntimeCommand({0x01u, 0x02u});
  persistent.safety_level = ReceiverCommandSafetyLevel::kPersistent;
  const auto rejected = dispatcher.Dispatch(persistent);
  ctx.Expect(rejected.status == DispatchStatus::kRejectedSafety &&
                 sink.written_bytes().empty(),
             "persistent commands should be rejected without explicit confirmation");

  persistent.explicit_safety_confirmation = true;
  const auto accepted = dispatcher.Dispatch(persistent);
  ctx.Expect(accepted.status == DispatchStatus::kSent &&
                 sink.written_bytes() == std::vector<std::uint8_t>({0x01u, 0x02u}),
             "persistent commands should dispatch after explicit confirmation");
  ctx.Expect(dispatcher.metrics().commands_rejected_safety == 1u &&
                 dispatcher.metrics().commands_sent == 1u,
             "dispatcher metrics should distinguish safety rejections from successful sends");
}

void TestFactoryResetSafetyPolicy(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandDispatcher dispatcher(sink);

  ReceiverCommand reset = MakeTextRuntimeCommand("RESET");
  reset.kind = ReceiverCommandKind::kReset;
  reset.safety_level = ReceiverCommandSafetyLevel::kFactoryReset;

  const auto result = dispatcher.Dispatch(reset);
  ctx.Expect(result.status == DispatchStatus::kRejectedSafety &&
                 dispatcher.metrics().commands_rejected_safety == 1u &&
                 sink.written_bytes().empty(),
             "factory reset commands should be rejected without explicit confirmation");
}

void TestEmptyPayloadRejectedByDefault(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandDispatcher dispatcher(sink);

  ReceiverCommand empty;
  empty.kind = ReceiverCommandKind::kQuery;

  const auto rejected = dispatcher.Dispatch(empty);
  ctx.Expect(rejected.status == DispatchStatus::kRejectedInvalid &&
                 dispatcher.metrics().commands_rejected_invalid == 1u,
             "empty commands should be rejected by default");

  ReceiverCommandDispatcher allow_empty(
      sink, ReceiverCommandDispatcherConfig{true});
  const auto allowed = allow_empty.Dispatch(empty);
  ctx.Expect(allowed.status == DispatchStatus::kSent &&
                 allowed.bytes_written == 0u &&
                 allow_empty.metrics().commands_sent == 1u,
             "dispatcher config should optionally allow empty payload dispatch");
}

void TestWriteErrors(TestContext& ctx)
{
  MemoryByteSink sink;
  sink.InjectNextWriteError(TransportError::kWriteFailure);
  ReceiverCommandDispatcher dispatcher(sink);

  const auto result = dispatcher.Dispatch(MakeBinaryRuntimeCommand({0xDEu, 0xADu}));
  ctx.Expect(result.status == DispatchStatus::kWriteError &&
                 result.transport_error == TransportError::kWriteFailure &&
                 dispatcher.metrics().write_errors == 1u &&
                 dispatcher.metrics().commands_sent == 0u,
             "injected sink write errors should be surfaced as dispatch write errors");
}

void TestTextPayloadDispatch(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandDispatcher dispatcher(sink);

  const auto result = dispatcher.Dispatch(MakeTextRuntimeCommand("CFG,TEST\r\n"));
  ctx.Expect(result.status == DispatchStatus::kSent &&
                 sink.written_bytes() ==
                     std::vector<std::uint8_t>({'C', 'F', 'G', ',', 'T', 'E', 'S', 'T', '\r', '\n'}),
             "text payload commands should dispatch UTF-8/ASCII bytes without terminator changes");
}

void TestResetMetrics(TestContext& ctx)
{
  MemoryByteSink sink;
  ReceiverCommandDispatcher dispatcher(sink);
  dispatcher.Dispatch(MakeBinaryRuntimeCommand({0x42u}));
  dispatcher.ResetMetrics();

  ctx.Expect(dispatcher.metrics().commands_attempted == 0u &&
                 dispatcher.metrics().commands_sent == 0u &&
                 dispatcher.metrics().bytes_written == 0u &&
                 dispatcher.metrics().write_errors == 0u,
             "dispatcher metric reset should clear counters");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestRuntimeCommandDispatchSucceeds(ctx);
  TestPersistentSafetyPolicy(ctx);
  TestFactoryResetSafetyPolicy(ctx);
  TestEmptyPayloadRejectedByDefault(ctx);
  TestWriteErrors(ctx);
  TestTextPayloadDispatch(ctx);
  TestResetMetrics(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver command dispatcher tests passed\n";
  return EXIT_SUCCESS;
}
