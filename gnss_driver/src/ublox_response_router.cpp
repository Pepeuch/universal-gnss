#include "universal_gnss_driver/ublox_response_router.hpp"

#include <utility>

#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/ubx_parser.hpp"

namespace universal_gnss_driver
{

bool UbloxResponseRouter::ProcessUbxFrame(
    const universal_gnss_protocols::UbxFrame& frame)
{
  ++metrics_.frames_seen;

  const auto parsed = universal_gnss_protocols::ParseUbxAck(frame);
  if (parsed.status == universal_gnss_protocols::ParserStatus::kRecordReady &&
      parsed.record.has_value())
  {
    UbloxRoutedResponse routed_response;
    routed_response.response =
        MapUbxAckRecordToReceiverCommandResponse(*parsed.record);
    routed_response.ubx_target =
        UbxMessageIdentity{parsed.record->target_class_id, parsed.record->target_message_id};

    if (parsed.record->kind == universal_gnss_protocols::UbxAckMessageKind::kAck)
    {
      ++metrics_.ack_frames_seen;
    }
    else
    {
      ++metrics_.nak_frames_seen;
    }

    queued_responses_.push_back(std::move(routed_response));
    ++metrics_.responses_generated;
    return true;
  }

  if (parsed.status == universal_gnss_protocols::ParserStatus::kInvalidData ||
      parsed.status == universal_gnss_protocols::ParserStatus::kOverflow ||
      parsed.status == universal_gnss_protocols::ParserStatus::kTruncated)
  {
    ++metrics_.malformed_frames;
    return false;
  }

  ++metrics_.ignored_frames;
  return false;
}

bool UbloxResponseRouter::TryGetResponse(UbloxRoutedResponse& response) const
{
  if (queued_responses_.empty())
  {
    return false;
  }

  response = queued_responses_.front();
  return true;
}

bool UbloxResponseRouter::PopResponse(UbloxRoutedResponse& response)
{
  if (queued_responses_.empty())
  {
    return false;
  }

  response = std::move(queued_responses_.front());
  queued_responses_.pop_front();
  return true;
}

void UbloxResponseRouter::Reset()
{
  queued_responses_.clear();
  metrics_ = UbloxResponseRouterMetrics{};
}

std::size_t UbloxResponseRouter::pending_response_count() const
{
  return queued_responses_.size();
}

const UbloxResponseRouterMetrics& UbloxResponseRouter::metrics() const
{
  return metrics_;
}

}  // namespace universal_gnss_driver
