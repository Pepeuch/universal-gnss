#include "universal_gnss_mavros/mavlink_rtcm_encoder.hpp"

#include <algorithm>
#include <iterator>

namespace universal_gnss_mavros
{

MavlinkRtcmEncodeResult MavlinkRtcmEncoder::Encode(const std::vector<std::uint8_t>& frame)
{
  MavlinkRtcmEncodeResult result;

  if (frame.empty())
  {
    result.status = MavlinkRtcmEncodeStatus::kEmptyFrame;
    return result;
  }

  if (frame.size() > kMaxFrameSize)
  {
    result.status = MavlinkRtcmEncodeStatus::kFrameTooLarge;
    return result;
  }

  const std::uint8_t sequence_bits =
      static_cast<std::uint8_t>(sequence_.fetch_add(1u) & 0x1Fu) << 3u;

  if (frame.size() <= MavlinkRtcmFragment::kPayloadCapacity)
  {
    MavlinkRtcmFragment fragment;
    fragment.flags = sequence_bits;
    fragment.len = static_cast<std::uint8_t>(frame.size());
    std::copy(frame.begin(), frame.end(), fragment.data.begin());
    result.fragments.push_back(fragment);
    return result;
  }

  auto current = frame.begin();
  const auto end = frame.end();

  for (std::uint8_t fragment_id = 0u;
       fragment_id < kMaxFragments && current != end;
       ++fragment_id)
  {
    MavlinkRtcmFragment fragment;
    const auto remaining = static_cast<std::size_t>(std::distance(current, end));
    const auto length = std::min(remaining, MavlinkRtcmFragment::kPayloadCapacity);

    fragment.flags = 0x01u;  // Bit 0 marks a fragmented RTCM frame.
    fragment.flags |= static_cast<std::uint8_t>(fragment_id << 1u);
    fragment.flags |= sequence_bits;
    fragment.len = static_cast<std::uint8_t>(length);

    std::copy_n(current, length, fragment.data.begin());
    result.fragments.push_back(fragment);
    std::advance(current, static_cast<std::ptrdiff_t>(length));
  }

  return result;
}

}  // namespace universal_gnss_mavros
