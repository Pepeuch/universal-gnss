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
      AppendByte(byte, timestamp_ns);
      return ParserResult<UnicoreBinaryFrame>::NeedMoreData();
    }

    if (byte == kUnicoreBinarySync1)
    {
      buffer_.assign(1u, byte);
      byte_timestamps_.assign(1u, timestamp_ns);
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
      AppendByte(byte, timestamp_ns);
      return ParserResult<UnicoreBinaryFrame>::NeedMoreData();
    }

    if (byte == kUnicoreBinarySync1)
    {
      buffer_.assign(1u, byte);
      byte_timestamps_.assign(1u, timestamp_ns);
      frame_timestamp_ns_ = timestamp_ns;
      expected_frame_size_ = 0u;
      return ParserResult<UnicoreBinaryFrame>::NeedMoreData();
    }

    Reset();
    return ParserResult<UnicoreBinaryFrame>::Skipped();
  }

  AppendByte(byte, timestamp_ns);

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
    const auto recovered = FindEmbeddedValidFrame();
    Reset();
    if (recovered.has_value())
    {
      return ParserResult<UnicoreBinaryFrame>::RecordReady(*recovered);
    }
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
  byte_timestamps_.clear();
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

  AppendByte(byte, timestamp_ns);
  frame_timestamp_ns_ = timestamp_ns;
  expected_frame_size_ = 0u;
  return ParserResult<UnicoreBinaryFrame>::NeedMoreData();
}

void UnicoreBinaryFrameFramer::AppendByte(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  buffer_.push_back(byte);
  byte_timestamps_.push_back(timestamp_ns);
}

std::optional<UnicoreBinaryFrame> UnicoreBinaryFrameFramer::FindEmbeddedValidFrame() const
{
  for (std::size_t frame_offset = 1u;
       frame_offset + kUnicoreBinaryHeaderSize + kUnicoreBinaryCrcSize <= buffer_.size();
       ++frame_offset)
  {
    if (buffer_[frame_offset] != kUnicoreBinarySync1 ||
        buffer_[frame_offset + 1u] != kUnicoreBinarySync2 ||
        buffer_[frame_offset + 2u] != kUnicoreBinarySync3)
    {
      continue;
    }

    const std::size_t payload_size =
        static_cast<std::size_t>(ReadLittleEndian16(buffer_.data() + frame_offset + 6u));
    const std::size_t frame_size =
        kUnicoreBinaryHeaderSize + payload_size + kUnicoreBinaryCrcSize;
    if (frame_size > max_frame_length_ || frame_size > buffer_.size() - frame_offset)
    {
      continue;
    }

    const std::uint32_t expected_crc = ReadLittleEndian32(
        buffer_.data() + frame_offset + frame_size - kUnicoreBinaryCrcSize);
    if (!ValidateUnicoreBinaryCrc32(
            buffer_.data() + frame_offset,
            frame_size - kUnicoreBinaryCrcSize,
            expected_crc))
    {
      continue;
    }

    return BuildFrame(frame_offset, frame_size, byte_timestamps_[frame_offset]);
  }

  return std::nullopt;
}

UnicoreBinaryFrame UnicoreBinaryFrameFramer::BuildFrame() const
{
  return BuildFrame(0u, buffer_.size(), frame_timestamp_ns_);
}

UnicoreBinaryFrame UnicoreBinaryFrameFramer::BuildFrame(
    std::size_t frame_offset,
    std::size_t frame_size,
    std::optional<ProtocolTimestampNs> timestamp_ns) const
{
  UnicoreBinaryFrame frame;
  frame.timestamp_ns = timestamp_ns;
  frame.raw_bytes.assign(
      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_offset),
      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_offset + frame_size));
  frame.cpu_idle = buffer_[frame_offset + 3u];
  frame.message_id = ReadLittleEndian16(buffer_.data() + frame_offset + 4u);
  frame.payload_length = ReadLittleEndian16(buffer_.data() + frame_offset + 6u);
  frame.time_ref = buffer_[frame_offset + 8u];
  frame.time_status = buffer_[frame_offset + 9u];
  frame.week_number = ReadLittleEndian16(buffer_.data() + frame_offset + 10u);
  frame.milliseconds_of_week = ReadLittleEndian32(buffer_.data() + frame_offset + 12u);
  frame.header_version = ReadLittleEndian32(buffer_.data() + frame_offset + 16u);
  frame.reserved = buffer_[frame_offset + 20u];
  frame.leap_seconds = buffer_[frame_offset + 21u];
  frame.delay_ms = ReadLittleEndian16(buffer_.data() + frame_offset + 22u);
  frame.payload.assign(
      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_offset + kUnicoreBinaryHeaderSize),
      buffer_.begin() + static_cast<std::ptrdiff_t>(
          frame_offset + frame_size - kUnicoreBinaryCrcSize));
  frame.reported_crc32 =
      ReadLittleEndian32(buffer_.data() + frame_offset + frame_size - kUnicoreBinaryCrcSize);
  frame.computed_crc32 =
      ComputeUnicoreBinaryCrc32(
          buffer_.data() + frame_offset, frame_size - kUnicoreBinaryCrcSize);
  frame.checksum_status = (frame.reported_crc32 == frame.computed_crc32)
                              ? ChecksumStatus::kValid
                              : ChecksumStatus::kInvalid;
  return frame;
}

}  // namespace universal_gnss_protocols
