#include "universal_gnss_transport/rtcm_frame_writer.hpp"

#include <algorithm>
#include <utility>

namespace universal_gnss_transport {

RtcmFrameWriter::RtcmFrameWriter(const std::size_t capacity) : capacity_(capacity) {}

bool RtcmFrameWriter::Enqueue(Frame frame)
{
  if (frame.data.empty() || pending_.size() >= capacity_)
  {
    return false;
  }
  pending_.push_back(PendingFrame{std::move(frame), 0u});
  return true;
}

RtcmFrameWriter::FlushOutcome RtcmFrameWriter::Flush(ByteSink& sink)
{
  FlushOutcome outcome;
  if (pending_.empty())
  {
    return outcome;
  }
  if (!sink.IsOpen())
  {
    outcome.result = FlushResult::kFailed;
    outcome.status = TransportStatus::kClosed;
    outcome.error = TransportError::kClosed;
    Abandon();
    return outcome;
  }

  while (!pending_.empty())
  {
    PendingFrame& pending = pending_.front();
    const std::size_t remaining = pending.frame.data.size() - pending.offset;
    const WriteResult write = sink.Write(
        pending.frame.data.data() + static_cast<std::ptrdiff_t>(pending.offset), remaining);
    const std::size_t accepted = std::min(write.bytes_written, remaining);
    pending.offset += accepted;
    outcome.bytes_written += accepted;

    if (write.bytes_written > remaining || write.status != TransportStatus::kOk)
    {
      outcome.result = FlushResult::kFailed;
      outcome.status = write.status;
      outcome.error = write.bytes_written > remaining ? TransportError::kWriteFailure : write.error;
      Abandon();
      return outcome;
    }
    if (pending.offset == pending.frame.data.size())
    {
      outcome.last_message_type = pending.frame.message_type;
      ++outcome.frames_written;
      pending_.pop_front();
      continue;
    }
    if (accepted == 0u)
    {
      outcome.result = FlushResult::kBlocked;
      return outcome;
    }
  }
  return outcome;
}

void RtcmFrameWriter::Abandon() { pending_.clear(); }
std::size_t RtcmFrameWriter::size() const { return pending_.size(); }
std::size_t RtcmFrameWriter::capacity() const { return capacity_; }
bool RtcmFrameWriter::empty() const { return pending_.empty(); }

} // namespace universal_gnss_transport
