#include "universal_gnss_protocols/ubx_framer.hpp"

#include "universal_gnss_protocols/ubx_checksum.hpp"

namespace universal_gnss_protocols
{

namespace
{

constexpr std::uint8_t kUbxSync1 = 0xB5u;
constexpr std::uint8_t kUbxSync2 = 0x62u;
constexpr std::size_t kUbxHeaderSize = 6u;
constexpr std::size_t kUbxChecksumSize = 2u;

}  // namespace

UbxFrameFramer::UbxFrameFramer(std::size_t max_frame_length)
    : max_frame_length_(max_frame_length)
{
}

ParserResult<UbxFrame> UbxFrameFramer::PushByte(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  if (buffer_.empty())
  {
    return StartSync(byte, timestamp_ns);
  }

  if (buffer_.size() == 1u)
  {
    if (byte == kUbxSync2)
    {
      AppendByte(byte, timestamp_ns);
      return ParserResult<UbxFrame>::NeedMoreData();
    }

    if (byte == kUbxSync1)
    {
      buffer_.assign(1u, byte);
      byte_timestamps_.assign(1u, timestamp_ns);
      frame_timestamp_ns_ = timestamp_ns;
      return ParserResult<UbxFrame>::NeedMoreData();
    }

    Reset();
    return ParserResult<UbxFrame>::Skipped();
  }

  AppendByte(byte, timestamp_ns);
  if (buffer_.size() == kUbxHeaderSize)
  {
    const std::size_t payload_size =
        static_cast<std::size_t>(buffer_[4]) |
        (static_cast<std::size_t>(buffer_[5]) << 8);
    expected_frame_size_ = kUbxHeaderSize + payload_size + kUbxChecksumSize;

    if (expected_frame_size_ > max_frame_length_)
    {
      Reset();
      return ParserResult<UbxFrame>::Overflow();
    }
  }

  if (!buffer_.empty() && buffer_.size() > max_frame_length_)
  {
    Reset();
    return ParserResult<UbxFrame>::Overflow();
  }

  if (expected_frame_size_ == 0u || buffer_.size() < expected_frame_size_)
  {
    return ParserResult<UbxFrame>::NeedMoreData();
  }

  UbxFrame frame = BuildFrame();
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    const auto recovered = FindEmbeddedValidFrame();
    Reset();
    if (recovered.has_value())
    {
      return ParserResult<UbxFrame>::RecordReady(*recovered);
    }
    return ParserResult<UbxFrame>::RecordReady(std::move(frame));
  }

  Reset();
  return ParserResult<UbxFrame>::RecordReady(std::move(frame));
}

ParserResult<UbxFrame> UbxFrameFramer::Finalize()
{
  if (buffer_.empty())
  {
    return ParserResult<UbxFrame>{};
  }

  Reset();
  return ParserResult<UbxFrame>::Truncated();
}

void UbxFrameFramer::Reset()
{
  buffer_.clear();
  byte_timestamps_.clear();
  frame_timestamp_ns_.reset();
  expected_frame_size_ = 0u;
}

ParserResult<UbxFrame> UbxFrameFramer::StartSync(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  if (byte != kUbxSync1)
  {
    return ParserResult<UbxFrame>::Skipped();
  }

  AppendByte(byte, timestamp_ns);
  frame_timestamp_ns_ = timestamp_ns;
  expected_frame_size_ = 0u;
  return ParserResult<UbxFrame>::NeedMoreData();
}

void UbxFrameFramer::AppendByte(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  buffer_.push_back(byte);
  byte_timestamps_.push_back(timestamp_ns);
}

std::optional<UbxFrame> UbxFrameFramer::FindEmbeddedValidFrame() const
{
  for (std::size_t frame_offset = 1u;
       frame_offset + kUbxHeaderSize + kUbxChecksumSize <= buffer_.size();
       ++frame_offset)
  {
    if (buffer_[frame_offset] != kUbxSync1 || buffer_[frame_offset + 1u] != kUbxSync2)
    {
      continue;
    }

    const std::size_t payload_size =
        static_cast<std::size_t>(buffer_[frame_offset + 4u]) |
        (static_cast<std::size_t>(buffer_[frame_offset + 5u]) << 8);
    const std::size_t frame_size = kUbxHeaderSize + payload_size + kUbxChecksumSize;
    if (frame_size > max_frame_length_ || frame_size > buffer_.size() - frame_offset)
    {
      continue;
    }

    UbxFrame candidate = BuildFrame(
        frame_offset, frame_size, byte_timestamps_[frame_offset]);
    if (candidate.checksum_status == ChecksumStatus::kValid)
    {
      return candidate;
    }
  }

  return std::nullopt;
}

UbxFrame UbxFrameFramer::BuildFrame() const
{
  return BuildFrame(0u, buffer_.size(), frame_timestamp_ns_);
}

UbxFrame UbxFrameFramer::BuildFrame(
    std::size_t frame_offset,
    std::size_t frame_size,
    std::optional<ProtocolTimestampNs> timestamp_ns) const
{
  UbxFrame frame;
  frame.timestamp_ns = timestamp_ns;
  frame.raw_bytes.assign(
      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_offset),
      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_offset + frame_size));
  frame.class_id = buffer_[frame_offset + 2u];
  frame.message_id = buffer_[frame_offset + 3u];

  const std::size_t payload_size = frame_size - kUbxHeaderSize - kUbxChecksumSize;
  frame.payload.assign(
      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_offset + kUbxHeaderSize),
      buffer_.begin() +
          static_cast<std::ptrdiff_t>(frame_offset + kUbxHeaderSize + payload_size));

  frame.reported_ck_a = buffer_[frame_offset + frame_size - 2u];
  frame.reported_ck_b = buffer_[frame_offset + frame_size - 1u];
  const UbxChecksum computed =
      ComputeUbxChecksum(
          buffer_.data() + frame_offset + 2u, frame_size - 2u - kUbxChecksumSize);
  frame.computed_ck_a = computed.ck_a;
  frame.computed_ck_b = computed.ck_b;
  frame.checksum_status =
      ((*frame.reported_ck_a == computed.ck_a) && (*frame.reported_ck_b == computed.ck_b))
          ? ChecksumStatus::kValid
          : ChecksumStatus::kInvalid;
  return frame;
}

}  // namespace universal_gnss_protocols
