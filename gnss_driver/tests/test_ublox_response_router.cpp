#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command_response.hpp"
#include "universal_gnss_driver/ublox_response_router.hpp"
#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"

namespace
{

using universal_gnss_driver::ReceiverCommandResponseKind;
using universal_gnss_driver::UbloxResponseRouter;
using universal_gnss_driver::UbloxRoutedResponse;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;

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

UbxFrame BuildUbxFrame(std::uint8_t class_id,
                       std::uint8_t message_id,
                       const std::vector<std::uint8_t>& payload,
                       const bool valid_checksum = true,
                       const std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  std::vector<std::uint8_t> bytes;
  bytes.reserve(6u + payload.size() + 2u);
  bytes.push_back(0xB5u);
  bytes.push_back(0x62u);
  bytes.push_back(class_id);
  bytes.push_back(message_id);
  bytes.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((payload.size() >> 8u) & 0xFFu));
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const auto checksum =
      universal_gnss_protocols::ComputeUbxChecksum(bytes.data() + 2u, bytes.size() - 2u);
  bytes.push_back(valid_checksum ? checksum.ck_a
                                 : static_cast<std::uint8_t>(checksum.ck_a ^ 0x01u));
  bytes.push_back(checksum.ck_b);

  UbxFrameFramer framer;
  universal_gnss_protocols::ParserResult<UbxFrame> result;
  for (const auto byte : bytes)
  {
    result = framer.PushByte(byte, timestamp_ns);
  }

  if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
  {
    std::cerr << "FAILED: test setup could not frame UBX message\n";
    std::exit(EXIT_FAILURE);
  }

  return *result.record;
}

void TestAckFrameGeneratesAckResponse(TestContext& ctx)
{
  UbloxResponseRouter router;
  const bool generated =
      router.ProcessUbxFrame(BuildUbxFrame(0x05u, 0x01u, {0x06u, 0x8Au}, true, 1111));

  UbloxRoutedResponse routed_response;
  ctx.Expect(generated && router.TryGetResponse(routed_response),
             "ACK-ACK frames should generate a queued response");
  ctx.Expect(routed_response.response.kind == ReceiverCommandResponseKind::kAck &&
                 routed_response.response.timestamp_ns == std::optional<std::int64_t>(1111) &&
                 routed_response.ubx_target.has_value() &&
                 routed_response.ubx_target->class_id == 0x06u &&
                 routed_response.ubx_target->message_id == 0x8Au,
             "ACK-ACK routing should preserve the ACK response kind, timestamp, and UBX target");
  ctx.Expect(router.metrics().frames_seen == 1u &&
                 router.metrics().ack_frames_seen == 1u &&
                 router.metrics().responses_generated == 1u &&
                 router.pending_response_count() == 1u,
             "ACK routing should update metrics and queue depth");
}

void TestNakFrameGeneratesNakResponse(TestContext& ctx)
{
  UbloxResponseRouter router;
  const bool generated =
      router.ProcessUbxFrame(BuildUbxFrame(0x05u, 0x00u, {0x06u, 0x8Bu}, true, 2222));

  UbloxRoutedResponse routed_response;
  ctx.Expect(generated && router.PopResponse(routed_response),
             "ACK-NAK frames should generate a poppable response");
  ctx.Expect(routed_response.response.kind == ReceiverCommandResponseKind::kNak &&
                 routed_response.response.timestamp_ns == std::optional<std::int64_t>(2222) &&
                 routed_response.ubx_target.has_value() &&
                 routed_response.ubx_target->message_id == 0x8Bu,
             "ACK-NAK routing should preserve the NAK response kind, timestamp, and target id");
  ctx.Expect(router.metrics().nak_frames_seen == 1u &&
                 router.metrics().responses_generated == 1u &&
                 router.pending_response_count() == 0u,
             "NAK routing should update NAK metrics and empty the queue after pop");
}

void TestNavPvtIgnored(TestContext& ctx)
{
  UbloxResponseRouter router;
  const bool generated =
      router.ProcessUbxFrame(BuildUbxFrame(0x01u, 0x07u, std::vector<std::uint8_t>(92u, 0u)));

  UbloxRoutedResponse routed_response;
  ctx.Expect(!generated && !router.TryGetResponse(routed_response),
             "non-response UBX frames such as NAV-PVT should be ignored cleanly");
  ctx.Expect(router.metrics().frames_seen == 1u &&
                 router.metrics().ignored_frames == 1u &&
                 router.metrics().responses_generated == 0u,
             "ignored UBX runtime frames should update ignored metrics only");
}

void TestMalformedAckIgnored(TestContext& ctx)
{
  UbloxResponseRouter router;
  const bool bad_checksum =
      router.ProcessUbxFrame(BuildUbxFrame(0x05u, 0x01u, {0x06u, 0x8Au}, false));
  const bool wrong_payload =
      router.ProcessUbxFrame(BuildUbxFrame(0x05u, 0x00u, {0x06u}));

  UbloxRoutedResponse routed_response;
  ctx.Expect(!bad_checksum && !wrong_payload && !router.TryGetResponse(routed_response),
             "malformed ACK frames should not generate routed responses");
  ctx.Expect(router.metrics().frames_seen == 2u &&
                 router.metrics().malformed_frames == 2u &&
                 router.metrics().responses_generated == 0u &&
                 router.metrics().ignored_frames == 0u,
             "malformed ACK frames should update malformed metrics without touching ignored counts");
}

void TestResponseQueueBehavior(TestContext& ctx)
{
  UbloxResponseRouter router;
  router.ProcessUbxFrame(BuildUbxFrame(0x05u, 0x01u, {0x06u, 0x8Au}, true, 3001));
  router.ProcessUbxFrame(BuildUbxFrame(0x05u, 0x00u, {0x06u, 0x8Bu}, true, 3002));

  UbloxRoutedResponse front;
  UbloxRoutedResponse second;
  ctx.Expect(router.pending_response_count() == 2u && router.TryGetResponse(front),
             "queued responses should allow peeking at the front response");
  ctx.Expect(front.response.kind == ReceiverCommandResponseKind::kAck &&
                 front.response.timestamp_ns == std::optional<std::int64_t>(3001),
             "queue peeking should expose responses in FIFO order");

  ctx.Expect(router.PopResponse(front) && router.PopResponse(second),
             "queued responses should be poppable in FIFO order");
  ctx.Expect(front.response.kind == ReceiverCommandResponseKind::kAck &&
                 second.response.kind == ReceiverCommandResponseKind::kNak &&
                 router.pending_response_count() == 0u,
             "queue popping should preserve FIFO response ordering");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestAckFrameGeneratesAckResponse(ctx);
  TestNakFrameGeneratesNakResponse(ctx);
  TestNavPvtIgnored(ctx);
  TestMalformedAckIgnored(ctx);
  TestResponseQueueBehavior(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver u-blox response router tests passed\n";
  return EXIT_SUCCESS;
}
