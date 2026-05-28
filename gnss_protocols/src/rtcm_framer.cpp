#include "universal_gnss_protocols/rtcm_framer.hpp"

#include "universal_gnss_protocols/rtcm_crc24q.hpp"

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
      frame_timestamp_ns_ = timestamp_ns;
      expected_frame_size_ = 0u;
      return ParserResult<RtcmFrame>::NeedMoreData();
    }

    Reset();
    return ParserResult<RtcmFrame>::InvalidData();
  }

  buffer_.push_back(byte);

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

  buffer_.push_back(byte);
  frame_timestamp_ns_ = timestamp_ns;
  expected_frame_size_ = 0u;
  return ParserResult<RtcmFrame>::NeedMoreData();
}

RtcmFrame RtcmFrameFramer::BuildFrame() const
{
  RtcmFrame frame;
  frame.timestamp_ns = frame_timestamp_ns_;
  frame.raw_bytes = buffer_;

  const std::size_t payload_size = buffer_.size() - kRtcmHeaderSize - kRtcmCrcSize;
  frame.payload.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(kRtcmHeaderSize),
                       buffer_.begin() + static_cast<std::ptrdiff_t>(kRtcmHeaderSize + payload_size));

  if (frame.payload.size() >= 2u)
  {
    frame.message_type =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(frame.payload[0]) << 4) |
                                   (static_cast<std::uint16_t>(frame.payload[1]) >> 4));
  }

  frame.reported_crc24q =
      (static_cast<std::uint32_t>(buffer_[buffer_.size() - 3]) << 16) |
      (static_cast<std::uint32_t>(buffer_[buffer_.size() - 2]) << 8) |
      static_cast<std::uint32_t>(buffer_[buffer_.size() - 1]);
  frame.computed_crc24q = ComputeRtcmCrc24Q(buffer_.data(), buffer_.size() - kRtcmCrcSize);
  frame.checksum_status = (frame.reported_crc24q == frame.computed_crc24q)
                              ? ChecksumStatus::kValid
                              : ChecksumStatus::kInvalid;
  return frame;
}

}  // namespace universal_gnss_protocols
