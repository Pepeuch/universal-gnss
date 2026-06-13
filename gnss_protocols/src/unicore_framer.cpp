#include "universal_gnss_protocols/unicore_framer.hpp"

#include <cctype>
#include <string_view>

#include "universal_gnss_protocols/unicore_binary_framer.hpp"

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

std::uint8_t HexNibble(const char c)
{
  if (c >= '0' && c <= '9')
  {
    return static_cast<std::uint8_t>(c - '0');
  }

  const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return static_cast<std::uint8_t>(10 + upper - 'A');
}

bool TryParseHex32(const std::string_view text, std::uint32_t& value)
{
  if (text.size() != 8u)
  {
    return false;
  }

  value = 0u;
  for (const char c : text)
  {
    if (!std::isxdigit(static_cast<unsigned char>(c)))
    {
      return false;
    }

    value = static_cast<std::uint32_t>((value << 4u) | HexNibble(c));
  }

  return true;
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

  if (frame.sync_char != '#')
  {
    return frame;
  }

  const std::string_view payload_text(
      reinterpret_cast<const char*>(frame.payload.data()),
      frame.payload.size());
  const std::size_t star = payload_text.rfind('*');
  if (star == std::string_view::npos)
  {
    frame.checksum_status = ChecksumStatus::kMissing;
    return frame;
  }

  if (star + 9u != payload_text.size())
  {
    frame.checksum_status = ChecksumStatus::kInvalid;
    return frame;
  }

  if (!TryParseHex32(payload_text.substr(star + 1u, 8u), frame.reported_crc32))
  {
    frame.checksum_status = ChecksumStatus::kInvalid;
    return frame;
  }

  frame.computed_crc32 = ComputeUnicoreBinaryCrc32(
      reinterpret_cast<const std::uint8_t*>(payload_text.data()),
      star);
  frame.checksum_status =
      frame.reported_crc32 == frame.computed_crc32 ? ChecksumStatus::kValid
                                                   : ChecksumStatus::kInvalid;

  return frame;
}

bool UnicoreFrameFramer::IsSyncByte(std::uint8_t byte)
{
  return byte == '#' || byte == '$' || byte == '%';
}

}  // namespace universal_gnss_protocols
