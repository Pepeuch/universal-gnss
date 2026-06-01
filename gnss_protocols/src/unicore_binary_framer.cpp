#include "universal_gnss_protocols/unicore_binary_framer.hpp"

namespace universal_gnss_protocols
{

namespace
{

std::uint16_t ReadLittleEndian16(const std::uint8_t* data)
{
  return static_cast<std::uint16_t>(data[0]) |
         (static_cast<std::uint16_t>(data[1]) << 8);
}

std::uint32_t ReadLittleEndian32(const std::uint8_t* data)
{
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) |
         (static_cast<std::uint32_t>(data[3]) << 24);
}

}  // namespace

std::uint32_t ComputeUnicoreBinaryCrc32(const std::uint8_t* data, std::size_t size)
{
  std::uint32_t crc = 0u;
  for (std::size_t i = 0; i < size; ++i)
  {
    crc ^= static_cast<std::uint32_t>(data[i]);
    for (std::uint8_t bit = 0; bit < 8u; ++bit)
    {
      const bool lsb_set = (crc & 0x1u) != 0u;
      crc >>= 1u;
      if (lsb_set)
      {
        crc ^= 0xEDB88320u;
      }
    }
  }
  return crc;
}

bool ValidateUnicoreBinaryCrc32(
    const std::uint8_t* data,
    std::size_t size,
    std::uint32_t expected_crc32)
{
  return ComputeUnicoreBinaryCrc32(data, size) == expected_crc32;
}

UnicoreBinaryFrameFramer::UnicoreBinaryFrameFramer(std::size_t max_frame_length)
    : max_frame_length_(max_frame_length)
{
}

ParserResult<UnicoreBinaryFrame> UnicoreBinaryFrameFramer::PushByte(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  if (buffer_.empty())
  {
    return StartSync(byte, timestamp_ns);
  }

  if (buffer_.size() == 1u)
  {
    if (byte == kUnicoreBinarySync2)
    {
      buffer_.push_back(byte);
      return ParserResult<UnicoreBinaryFrame>::NeedMoreData();
    }

    if (byte == kUnicoreBinarySync1)
    {
      buffer_.assign(1u, byte);
      frame_timestamp_ns_ = timestamp_ns;
      expected_frame_size_ = 0u;
      return ParserResult<UnicoreBinaryFrame>::NeedMoreData();
    }

    Reset();
    return ParserResult<UnicoreBinaryFrame>::Skipped();
  }

  if (buffer_.size() == 2u)
  {
    if (byte == kUnicoreBinarySync3)
    {
      buffer_.push_back(byte);
      return ParserResult<UnicoreBinaryFrame>::NeedMoreData();
    }

    if (byte == kUnicoreBinarySync1)
    {
      buffer_.assign(1u, byte);
      frame_timestamp_ns_ = timestamp_ns;
      expected_frame_size_ = 0u;
      return ParserResult<UnicoreBinaryFrame>::NeedMoreData();
    }

    Reset();
    return ParserResult<UnicoreBinaryFrame>::Skipped();
  }

  buffer_.push_back(byte);

  if (buffer_.size() == kUnicoreBinaryHeaderSize)
  {
    const std::size_t payload_size =
        static_cast<std::size_t>(ReadLittleEndian16(buffer_.data() + 6u));
    expected_frame_size_ = kUnicoreBinaryHeaderSize + payload_size + kUnicoreBinaryCrcSize;

    if (expected_frame_size_ > max_frame_length_)
    {
      Reset();
      return ParserResult<UnicoreBinaryFrame>::Overflow();
    }
  }

  if (!buffer_.empty() && buffer_.size() > max_frame_length_)
  {
    Reset();
    return ParserResult<UnicoreBinaryFrame>::Overflow();
  }

  if (expected_frame_size_ == 0u || buffer_.size() < expected_frame_size_)
  {
    return ParserResult<UnicoreBinaryFrame>::NeedMoreData();
  }

  const std::uint32_t expected_crc =
      ReadLittleEndian32(buffer_.data() + buffer_.size() - kUnicoreBinaryCrcSize);
  if (!ValidateUnicoreBinaryCrc32(
          buffer_.data(), buffer_.size() - kUnicoreBinaryCrcSize, expected_crc))
  {
    Reset();
    return ParserResult<UnicoreBinaryFrame>::InvalidData();
  }

  UnicoreBinaryFrame frame = BuildFrame();
  Reset();
  return ParserResult<UnicoreBinaryFrame>::RecordReady(std::move(frame));
}

ParserResult<UnicoreBinaryFrame> UnicoreBinaryFrameFramer::Finalize()
{
  if (buffer_.empty())
  {
    return ParserResult<UnicoreBinaryFrame>{};
  }

  Reset();
  return ParserResult<UnicoreBinaryFrame>::Truncated();
}

void UnicoreBinaryFrameFramer::Reset()
{
  buffer_.clear();
  frame_timestamp_ns_.reset();
  expected_frame_size_ = 0u;
}

ParserResult<UnicoreBinaryFrame> UnicoreBinaryFrameFramer::StartSync(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  if (byte != kUnicoreBinarySync1)
  {
    return ParserResult<UnicoreBinaryFrame>::Skipped();
  }

  buffer_.push_back(byte);
  frame_timestamp_ns_ = timestamp_ns;
  expected_frame_size_ = 0u;
  return ParserResult<UnicoreBinaryFrame>::NeedMoreData();
}

UnicoreBinaryFrame UnicoreBinaryFrameFramer::BuildFrame() const
{
  UnicoreBinaryFrame frame;
  frame.timestamp_ns = frame_timestamp_ns_;
  frame.raw_bytes = buffer_;
  frame.cpu_idle = buffer_[3];
  frame.message_id = ReadLittleEndian16(buffer_.data() + 4u);
  frame.payload_length = ReadLittleEndian16(buffer_.data() + 6u);
  frame.time_ref = buffer_[8];
  frame.time_status = buffer_[9];
  frame.week_number = ReadLittleEndian16(buffer_.data() + 10u);
  frame.milliseconds_of_week = ReadLittleEndian32(buffer_.data() + 12u);
  frame.header_version = ReadLittleEndian32(buffer_.data() + 16u);
  frame.reserved = buffer_[20];
  frame.leap_seconds = buffer_[21];
  frame.delay_ms = ReadLittleEndian16(buffer_.data() + 22u);
  frame.payload.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(kUnicoreBinaryHeaderSize),
                       buffer_.end() - static_cast<std::ptrdiff_t>(kUnicoreBinaryCrcSize));
  frame.reported_crc32 =
      ReadLittleEndian32(buffer_.data() + buffer_.size() - kUnicoreBinaryCrcSize);
  frame.computed_crc32 =
      ComputeUnicoreBinaryCrc32(buffer_.data(), buffer_.size() - kUnicoreBinaryCrcSize);
  frame.checksum_status = (frame.reported_crc32 == frame.computed_crc32)
                              ? ChecksumStatus::kValid
                              : ChecksumStatus::kInvalid;
  return frame;
}

}  // namespace universal_gnss_protocols
