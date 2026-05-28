#include "universal_gnss_protocols/unicore_framer.hpp"

namespace universal_gnss_protocols
{

namespace
{

std::size_t TrimLineEnding(const std::vector<std::uint8_t>& buffer)
{
  std::size_t end = buffer.size();
  while (end > 0 && (buffer[end - 1] == '\r' || buffer[end - 1] == '\n'))
  {
    --end;
  }
  return end;
}

}  // namespace

UnicoreFrameFramer::UnicoreFrameFramer(std::size_t max_frame_length)
    : max_frame_length_(max_frame_length)
{
}

ParserResult<UnicoreFrame> UnicoreFrameFramer::PushByte(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  if (buffer_.empty())
  {
    if (!IsSyncByte(byte))
    {
      return ParserResult<UnicoreFrame>::Skipped();
    }

    buffer_.push_back(byte);
    frame_timestamp_ns_ = timestamp_ns;
    return ParserResult<UnicoreFrame>::NeedMoreData();
  }

  buffer_.push_back(byte);
  if (buffer_.size() > max_frame_length_)
  {
    Reset();
    return ParserResult<UnicoreFrame>::Overflow();
  }

  if (byte != '\n')
  {
    return ParserResult<UnicoreFrame>::NeedMoreData();
  }

  UnicoreFrame frame = BuildFrame();
  Reset();
  return ParserResult<UnicoreFrame>::RecordReady(std::move(frame));
}

ParserResult<UnicoreFrame> UnicoreFrameFramer::Finalize()
{
  if (buffer_.empty())
  {
    return ParserResult<UnicoreFrame>{};
  }

  Reset();
  return ParserResult<UnicoreFrame>::Truncated();
}

void UnicoreFrameFramer::Reset()
{
  buffer_.clear();
  frame_timestamp_ns_.reset();
}

UnicoreFrame UnicoreFrameFramer::BuildFrame() const
{
  UnicoreFrame frame;
  frame.timestamp_ns = frame_timestamp_ns_;
  frame.raw_bytes = buffer_;
  frame.sync_char = static_cast<char>(buffer_.front());

  const std::size_t end = TrimLineEnding(buffer_);
  if (end > 1u)
  {
    frame.payload.assign(buffer_.begin() + 1, buffer_.begin() + static_cast<std::ptrdiff_t>(end));
    for (std::size_t i = 1; i < end; ++i)
    {
      const char c = static_cast<char>(buffer_[i]);
      if (c == ',' || c == ';' || c == '*' || c == ' ')
      {
        break;
      }
      frame.message_name.push_back(c);
    }
  }

  return frame;
}

bool UnicoreFrameFramer::IsSyncByte(std::uint8_t byte)
{
  return byte == '#' || byte == '$' || byte == '%';
}

}  // namespace universal_gnss_protocols
