// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_receiver_runtime.hpp"
namespace ug::embedded
{
using namespace universal_gnss_driver;
Receiver::Receiver(universal_gnss_transport::ByteSink& uart, ReceiverSessionConfig config)
    : session_(config), application_(uart)
{
}

void Receiver::Note(const ReceiverConfigApplicationResult& result)
{
  fence_.NoteDispatch(result);
}

bool Receiver::Start(std::vector<ReceiverCommand> commands, const std::int64_t now_ns)
{
  if (commands.empty() || commands.size() > 32 || now_ns < last_receipt_ns_ || indeterminate())
    return false;
  if (application_.state() == ReceiverConfigApplicationState::kRunning ||
      application_.state() == ReceiverConfigApplicationState::kWaitingForResponse)
    return false;
  // Reject the whole plan before any write; persist/reset always need confirmation.
  for (const auto& command : commands)
    if (!HasSafeDispatchApproval(command) || command.payload.binary.size() > 512 ||
        command.payload.text.size() > 512)
      return false;
  const auto result = application_.Start(std::move(commands), now_ns);
  Note(result);
  return result.state != ReceiverConfigApplicationState::kFailed;
}

bool Receiver::Feed(const std::uint8_t* bytes,
                    const std::size_t size,
                    const std::int64_t receipt_ns)
{
  if ((!bytes && size) || size > 512 || receipt_ns < last_receipt_ns_ || receipt_ns < 0)
    return false;
  last_receipt_ns_ = receipt_ns;
  session_.FeedBytes(bytes, size, receipt_ns);
  // One capture generation per batch. Responses coalesced with the ACK that
  // completes a command cannot acknowledge the next command dispatched later.
  const auto generation = fence_.BeginCapture();
  for (std::size_t i = 0; i < size; ++i)
  {
    const auto framed = ubx_.PushByte(bytes[i], receipt_ns);
    if (!ubx_generation_ && framed.status == universal_gnss_protocols::ParserStatus::kNeedMoreData)
      ubx_generation_ = generation;
    if (framed.status == universal_gnss_protocols::ParserStatus::kRecordReady && framed.record)
    {
      ublox_.ProcessUbxFrame(*framed.record, ubx_generation_.value_or(generation));
      UbloxRoutedResponse response;
      while (ublox_.PopResponse(response))
        if (fence_.IsEligible(response.response))
          Note(application_.ApplyResponse(response.response, {response.ubx_target}));
      ubx_generation_.reset();
    }
    else if (framed.status != universal_gnss_protocols::ParserStatus::kNeedMoreData)
      ubx_generation_.reset();
    if (line_size_ == 0 && !line_overflow_)
      line_generation_ = generation;
    if (bytes[i] == '\n')
    {
      if (!line_overflow_)
      {
        unicore_.ProcessLine({line_.data(), line_size_}, receipt_ns, line_generation_);
        ReceiverCommandResponse response;
        while (unicore_.PopResponse(response))
          if (fence_.IsEligible(response))
            Note(application_.ApplyResponse(response));
      }
      line_size_ = 0;
      line_overflow_ = false;
    }
    else if (line_size_ < line_.size())
      line_[line_size_++] = static_cast<char>(bytes[i]);
    else
      line_overflow_ = true;
  }
  return true;
}

void Receiver::Step(const std::int64_t now_ns)
{
  if (now_ns < last_receipt_ns_ || now_ns < 0)
    return;
  Note(application_.CheckTimeout(now_ns));
  if (application_.state() == ReceiverConfigApplicationState::kRunning)
    Note(application_.Step(now_ns));
}

void Receiver::InvalidateObservations()
{
  // A disconnected pending command cannot be made safe by resetting parsers.
  if (application_.transaction_engine().current_transaction())
    Note(application_.MarkTimeout());
  session_.Reset();
  ubx_.Reset();
  ublox_.Reset();
  unicore_.Reset();
  line_size_ = 0;
  line_overflow_ = false;
  ubx_generation_.reset();
  ++incarnation_;
}
}  // namespace ug::embedded
