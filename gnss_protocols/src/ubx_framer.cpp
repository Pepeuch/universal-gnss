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
      buffer_.push_back(byte);
      return ParserResult<UbxFrame>::NeedMoreData();
    }

    if (byte == kUbxSync1)
    {
      buffer_.assign(1u, byte);
      frame_timestamp_ns_ = timestamp_ns;
      return ParserResult<UbxFrame>::NeedMoreData();
    }

    Reset();
    return ParserResult<UbxFrame>::Skipped();
  }

  buffer_.push_back(byte);
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

  buffer_.push_back(byte);
  frame_timestamp_ns_ = timestamp_ns;
  expected_frame_size_ = 0u;
  return ParserResult<UbxFrame>::NeedMoreData();
}

UbxFrame UbxFrameFramer::BuildFrame() const
{
  UbxFrame frame;
  frame.timestamp_ns = frame_timestamp_ns_;
  frame.raw_bytes = buffer_;
  frame.class_id = buffer_[2];
  frame.message_id = buffer_[3];

  const std::size_t payload_size = buffer_.size() - kUbxHeaderSize - kUbxChecksumSize;
  frame.payload.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(kUbxHeaderSize),
                       buffer_.begin() +
                           static_cast<std::ptrdiff_t>(kUbxHeaderSize + payload_size));

  frame.reported_ck_a = buffer_[buffer_.size() - 2];
  frame.reported_ck_b = buffer_[buffer_.size() - 1];
  const UbxChecksum computed =
      ComputeUbxChecksum(buffer_.data() + 2, buffer_.size() - 2u - kUbxChecksumSize);
  frame.computed_ck_a = computed.ck_a;
  frame.computed_ck_b = computed.ck_b;
  frame.checksum_status =
      ((*frame.reported_ck_a == computed.ck_a) && (*frame.reported_ck_b == computed.ck_b))
          ? ChecksumStatus::kValid
          : ChecksumStatus::kInvalid;
  return frame;
}

}  // namespace universal_gnss_protocols
