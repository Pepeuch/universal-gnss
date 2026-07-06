#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss_driver/receiver_command_response.hpp"
#include "universal_gnss_driver/unicore_response_router.hpp"

namespace
{

using universal_gnss_driver::ReceiverCommandResponse;
using universal_gnss_driver::ReceiverCommandResponseKind;
using universal_gnss_driver::UnicoreResponseRouter;

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

void TestOkResponseMapsToTextOk(TestContext& ctx)
{
  UnicoreResponseRouter router;
  const bool generated = router.ProcessLine("$command,GPGGA 1,response: OK*\r\n", 1111);

  ReceiverCommandResponse response;
  ctx.Expect(generated && router.PopResponse(response),
             "documented $command accepted responses should generate a response");
  ctx.Expect(response.kind == ReceiverCommandResponseKind::kTextOk &&
                 response.timestamp_ns == std::optional<std::int64_t>(1111) &&
                 response.message == "$command,GPGGA 1,response: OK*",
             "accepted Unicore command responses should map to text_ok");
  ctx.Expect(router.metrics().lines_seen == 1u && router.metrics().ok_responses_seen == 1u &&
                 router.metrics().responses_generated == 1u,
             "accepted responses should update OK routing metrics");
}

void TestErrorResponseMapsToTextError(TestContext& ctx)
{
  UnicoreResponseRouter router;
  const bool generated = router.ProcessLine("unsupported command\r\n", 2222);

  ReceiverCommandResponse response;
  ctx.Expect(generated && router.TryGetResponse(response),
             "documented explicit Unicore failures should generate a response");
  ctx.Expect(response.kind == ReceiverCommandResponseKind::kTextError &&
                 response.timestamp_ns == std::optional<std::int64_t>(2222) &&
                 response.message == "unsupported command",
             "documented explicit Unicore failures should map to text_error");
  ctx.Expect(router.metrics().error_responses_seen == 1u &&
                 router.metrics().responses_generated == 1u &&
                 router.pending_response_count() == 1u,
             "error responses should update error routing metrics");
}

void TestCapturedPrefixedModeRoverAckMapsToTextOk(TestContext& ctx)
{
  UnicoreResponseRouter router;
  const std::string captured =
      std::string("[\x01", 2) + "$command,MODE ROVER SURVEY MOW,response: OK*21\r\n";
  const bool generated = router.ProcessLine(captured, 2525);

  ReceiverCommandResponse response;
  ctx.Expect(
      generated && router.PopResponse(response),
      "captured UM982 MODE ROVER SURVEY MOW acknowledgements should survive short mixed-stream "
      "prefixes");
  ctx.Expect(response.kind == ReceiverCommandResponseKind::kTextOk &&
                 response.timestamp_ns == std::optional<std::int64_t>(2525) &&
                 response.message == "$command,MODE ROVER SURVEY MOW,response: OK*21",
             "captured UM982 MODE ROVER SURVEY MOW acknowledgements should normalize to the clean "
             "response line");
  ctx.Expect(router.metrics().lines_seen == 1u && router.metrics().ok_responses_seen == 1u &&
                 router.metrics().responses_generated == 1u &&
                 router.metrics().malformed_lines == 0u,
             "captured prefixed acknowledgements should count as valid OK responses");
}

void TestCapturedLongPrefixedRtkTimeoutAckMapsToTextOk(TestContext& ctx)
{
  UnicoreResponseRouter router;
  const std::string captured = std::string(
                                   "\x00\x01\x02\x03\x04\x05\x06\x07"
                                   "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
                                   16) +
                               "$command,CONFIG RTK TIMEOUT 10,response: OK*63\r\n";
  const bool generated = router.ProcessLine(captured, 2626);

  ReceiverCommandResponse response;
  ctx.Expect(generated && router.PopResponse(response),
             "captured UM982 CONFIG RTK TIMEOUT acknowledgements should survive long mixed-stream "
             "prefixes");
  ctx.Expect(response.kind == ReceiverCommandResponseKind::kTextOk &&
                 response.timestamp_ns == std::optional<std::int64_t>(2626) &&
                 response.message == "$command,CONFIG RTK TIMEOUT 10,response: OK*63",
             "captured UM982 CONFIG RTK TIMEOUT acknowledgements should normalize to the clean "
             "response line");
  ctx.Expect(router.metrics().lines_seen == 1u && router.metrics().ok_responses_seen == 1u &&
                 router.metrics().responses_generated == 1u &&
                 router.metrics().malformed_lines == 0u,
             "captured long-prefixed acknowledgements should count as valid OK responses");
}

void TestTelemetryLineIgnored(TestContext& ctx)
{
  UnicoreResponseRouter router;
  const bool generated =
      router.ProcessLine("#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;SOL_COMPUTED,SINGLE\r\n");

  ReceiverCommandResponse response;
  ctx.Expect(!generated && !router.TryGetResponse(response),
             "runtime telemetry such as BESTNAVA should be ignored");
  ctx.Expect(router.metrics().lines_seen == 1u && router.metrics().ignored_lines == 1u &&
                 router.metrics().responses_generated == 0u,
             "ignored telemetry should only affect ignored-line metrics");
}

void TestGarbageAndMalformedHandling(TestContext& ctx)
{
  UnicoreResponseRouter router;
  const bool invalid_ack = router.ProcessLine("$command,BESTNAVA 0.2,response: FAIL*\r\n");
  const bool binary_garbage = router.ProcessLine(std::string("\x01\x02", 2));

  ReceiverCommandResponse response;
  ctx.Expect(!invalid_ack && !binary_garbage && !router.TryGetResponse(response),
             "invalid response-shaped lines and non-printable garbage should not route responses");
  ctx.Expect(router.metrics().lines_seen == 2u && router.metrics().malformed_lines == 2u &&
                 router.metrics().ignored_lines == 0u,
             "malformed response lines should update malformed metrics");
}

void TestResponseQueueFifoAndFeedBytes(TestContext& ctx)
{
  UnicoreResponseRouter router;
  router.FeedBytes("<OK\r\nunsupported ");
  router.FeedBytes("command\r\n", 3002);

  ReceiverCommandResponse first;
  ReceiverCommandResponse second;
  ctx.Expect(router.pending_response_count() == 2u && router.TryGetResponse(first),
             "feeding byte chunks should queue completed responses");
  ctx.Expect(first.kind == ReceiverCommandResponseKind::kTextOk && first.message == "<OK",
             "the first completed response should stay at the front of the queue");

  ctx.Expect(router.PopResponse(first) && router.PopResponse(second),
             "queued Unicore responses should pop in FIFO order");
  ctx.Expect(first.kind == ReceiverCommandResponseKind::kTextOk &&
                 second.kind == ReceiverCommandResponseKind::kTextError &&
                 second.timestamp_ns == std::optional<std::int64_t>(3002) &&
                 router.pending_response_count() == 0u,
             "byte-fed responses should preserve FIFO order and timestamps");
}

void TestResetClearsQueueAndMetrics(TestContext& ctx)
{
  UnicoreResponseRouter router;
  router.ProcessLine("<OK\r\n", 4444);
  router.Reset();

  ReceiverCommandResponse response;
  ctx.Expect(!router.TryGetResponse(response) && router.pending_response_count() == 0u,
             "reset should clear queued Unicore responses");
  ctx.Expect(router.metrics().lines_seen == 0u && router.metrics().ok_responses_seen == 0u &&
                 router.metrics().responses_generated == 0u &&
                 router.metrics().ignored_lines == 0u && router.metrics().malformed_lines == 0u,
             "reset should clear Unicore router metrics");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestOkResponseMapsToTextOk(ctx);
  TestErrorResponseMapsToTextError(ctx);
  TestCapturedPrefixedModeRoverAckMapsToTextOk(ctx);
  TestCapturedLongPrefixedRtkTimeoutAckMapsToTextOk(ctx);
  TestTelemetryLineIgnored(ctx);
  TestGarbageAndMalformedHandling(ctx);
  TestResponseQueueFifoAndFeedBytes(ctx);
  TestResetClearsQueueAndMetrics(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver Unicore response router tests passed\n";
  return EXIT_SUCCESS;
}
