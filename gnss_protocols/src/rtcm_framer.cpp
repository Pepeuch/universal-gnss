#include "universal_gnss_protocols/rtcm_framer.hpp"

#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"

namespace universal_gnss_protocols
{

namespace
{

constexpr std::uint8_t kRtcmPreamble = 0xD3u;
constexpr std::size_t kRtcmHeaderSize = 3u;
constexpr std::size_t kRtcmCrcSize = 3u;

}  // namespace

RtcmFrameFramer::RtcmFrameFramer(std::size_t max_frame_length)
    : max_frame_length_(max_frame_length)
{
}

ParserResult<RtcmFrame> RtcmFrameFramer::PushByte(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  if (buffer_.empty())
  {
    return StartFrame(byte, timestamp_ns);
  }

  if (buffer_.size() == 1u && (byte & 0xFCu) != 0u)
  {
    if (byte == kRtcmPreamble)
    {
      buffer_.assign(1u, byte);
      byte_timestamps_.assign(1u, timestamp_ns);
      frame_timestamp_ns_ = timestamp_ns;
      expected_frame_size_ = 0u;
      return ParserResult<RtcmFrame>::NeedMoreData();
    }

    Reset();
    return ParserResult<RtcmFrame>::InvalidData();
  }

  AppendByte(byte, timestamp_ns);

  if (buffer_.size() == kRtcmHeaderSize)
  {
    const std::size_t payload_size =
        (static_cast<std::size_t>(buffer_[1] & 0x03u) << 8) |
        static_cast<std::size_t>(buffer_[2]);
    expected_frame_size_ = kRtcmHeaderSize + payload_size + kRtcmCrcSize;

    if (expected_frame_size_ > max_frame_length_)
    {
      Reset();
      return ParserResult<RtcmFrame>::Overflow();
    }
  }

  if (!buffer_.empty() && buffer_.size() > max_frame_length_)
  {
    Reset();
    return ParserResult<RtcmFrame>::Overflow();
  }

  if (expected_frame_size_ == 0u || buffer_.size() < expected_frame_size_)
  {
    return ParserResult<RtcmFrame>::NeedMoreData();
  }

  RtcmFrame frame = BuildFrame();
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    const auto recovered = FindEmbeddedValidFrame();
    Reset();
    if (recovered.has_value())
    {
      return ParserResult<RtcmFrame>::RecordReady(*recovered);
    }
    return ParserResult<RtcmFrame>::RecordReady(std::move(frame));
  }

  Reset();
  return ParserResult<RtcmFrame>::RecordReady(std::move(frame));
}

ParserResult<RtcmFrame> RtcmFrameFramer::Finalize()
{
  if (buffer_.empty())
  {
    return ParserResult<RtcmFrame>{};
  }

  Reset();
  return ParserResult<RtcmFrame>::Truncated();
}

void RtcmFrameFramer::Reset()
{
  buffer_.clear();
  byte_timestamps_.clear();
  frame_timestamp_ns_.reset();
  expected_frame_size_ = 0u;
}

ParserResult<RtcmFrame> RtcmFrameFramer::StartFrame(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  if (byte != kRtcmPreamble)
  {
    return ParserResult<RtcmFrame>::Skipped();
  }

  AppendByte(byte, timestamp_ns);
  frame_timestamp_ns_ = timestamp_ns;
  expected_frame_size_ = 0u;
  return ParserResult<RtcmFrame>::NeedMoreData();
}

void RtcmFrameFramer::AppendByte(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  buffer_.push_back(byte);
  byte_timestamps_.push_back(timestamp_ns);
}

std::optional<RtcmFrame> RtcmFrameFramer::FindEmbeddedValidFrame() const
{
  for (std::size_t frame_offset = 1u;
       frame_offset + kRtcmHeaderSize + kRtcmCrcSize <= buffer_.size();
       ++frame_offset)
  {
    if (buffer_[frame_offset] != kRtcmPreamble ||
        (buffer_[frame_offset + 1u] & 0xFCu) != 0u)
    {
      continue;
    }

    const std::size_t payload_size =
        (static_cast<std::size_t>(buffer_[frame_offset + 1u] & 0x03u) << 8) |
        static_cast<std::size_t>(buffer_[frame_offset + 2u]);
    const std::size_t frame_size = kRtcmHeaderSize + payload_size + kRtcmCrcSize;
    if (frame_size > max_frame_length_ || frame_size > buffer_.size() - frame_offset)
    {
      continue;
    }

    RtcmFrame candidate = BuildFrame(
        frame_offset, frame_size, byte_timestamps_[frame_offset]);
    if (candidate.checksum_status == ChecksumStatus::kValid)
    {
      return candidate;
    }
  }

  return std::nullopt;
}

RtcmFrame RtcmFrameFramer::BuildFrame() const
{
  return BuildFrame(0u, buffer_.size(), frame_timestamp_ns_);
}

RtcmFrame RtcmFrameFramer::BuildFrame(
    std::size_t frame_offset,
    std::size_t frame_size,
    std::optional<ProtocolTimestampNs> timestamp_ns) const
{
  RtcmFrame frame;
  frame.timestamp_ns = timestamp_ns;
  frame.raw_bytes.assign(
      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_offset),
      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_offset + frame_size));

  const std::size_t payload_size = frame_size - kRtcmHeaderSize - kRtcmCrcSize;
  frame.payload.assign(
      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_offset + kRtcmHeaderSize),
      buffer_.begin() +
          static_cast<std::ptrdiff_t>(frame_offset + kRtcmHeaderSize + payload_size));

  if (const auto message_type = ExtractRtcmMessageType(frame.payload); message_type.has_value())
  {
    frame.message_type = *message_type;
  }

  frame.reported_crc24q =
      (static_cast<std::uint32_t>(buffer_[frame_offset + frame_size - 3u]) << 16) |
      (static_cast<std::uint32_t>(buffer_[frame_offset + frame_size - 2u]) << 8) |
      static_cast<std::uint32_t>(buffer_[frame_offset + frame_size - 1u]);
  frame.computed_crc24q = ComputeRtcmCrc24Q(
      buffer_.data() + frame_offset, frame_size - kRtcmCrcSize);
  frame.checksum_status = (frame.reported_crc24q == frame.computed_crc24q)
                              ? ChecksumStatus::kValid
                              : ChecksumStatus::kInvalid;
  return frame;
}

}  // namespace universal_gnss_protocols
