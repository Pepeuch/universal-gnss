#include "universal_gnss_mavros/mavlink_rtcm_encoder.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{

using universal_gnss_mavros::MavlinkRtcmEncodeStatus;
using universal_gnss_mavros::MavlinkRtcmEncoder;
using universal_gnss_mavros::MavlinkRtcmFragment;

int failures = 0;

#define EXPECT_TRUE(condition)                                                                     \
  do                                                                                               \
  {                                                                                                \
    if (!(condition))                                                                              \
    {                                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": expected " #condition "\n";                   \
      ++failures;                                                                                  \
    }                                                                                              \
  } while (false)

#define EXPECT_EQ(actual, expected)                                                                \
  do                                                                                               \
  {                                                                                                \
    if (!((actual) == (expected)))                                                                 \
    {                                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": equality failed: " #actual " vs "             \
                << #expected << "\n";                                                              \
      ++failures;                                                                                  \
    }                                                                                              \
  } while (false)

std::vector<std::uint8_t> MakeFrame(const std::size_t size)
{
  std::vector<std::uint8_t> frame(size);
  for (std::size_t index = 0u; index < size; ++index)
  {
    frame[index] = static_cast<std::uint8_t>(index & 0xFFu);
  }
  return frame;
}

void ExpectPayload(const MavlinkRtcmFragment& fragment,
                   const std::vector<std::uint8_t>& frame,
                   const std::size_t offset)
{
  EXPECT_TRUE(offset + fragment.len <= frame.size());
  for (std::size_t index = 0u; index < fragment.len; ++index)
  {
    EXPECT_EQ(fragment.data[index], frame[offset + index]);
  }
  for (std::size_t index = fragment.len; index < fragment.data.size(); ++index)
  {
    EXPECT_EQ(fragment.data[index], 0u);
  }
}

void TestSingleFragment()
{
  MavlinkRtcmEncoder encoder;
  const auto frame = MakeFrame(100u);
  const auto result = encoder.Encode(frame);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.fragments.size(), 1u);
  EXPECT_EQ(result.fragments[0].flags, 0u);
  EXPECT_EQ(result.fragments[0].len, 100u);
  ExpectPayload(result.fragments[0], frame, 0u);
}

void TestExactSingleFragmentLimit()
{
  MavlinkRtcmEncoder encoder;
  const auto frame = MakeFrame(MavlinkRtcmFragment::kPayloadCapacity);
  const auto result = encoder.Encode(frame);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.fragments.size(), 1u);
  EXPECT_EQ(result.fragments[0].flags, 0u);
  EXPECT_EQ(result.fragments[0].len, MavlinkRtcmFragment::kPayloadCapacity);
  ExpectPayload(result.fragments[0], frame, 0u);
}

void TestFragmentationAndFlags()
{
  MavlinkRtcmEncoder encoder;
  EXPECT_TRUE(encoder.Encode(MakeFrame(1u)).ok());  // Consume sequence 0.

  const auto frame = MakeFrame(181u);
  const auto result = encoder.Encode(frame);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.fragments.size(), 2u);
  EXPECT_EQ(result.fragments[0].flags, 0x09u);  // fragmented + fragment 0 + sequence 1
  EXPECT_EQ(result.fragments[1].flags, 0x0Bu);  // fragmented + fragment 1 + sequence 1
  EXPECT_EQ(result.fragments[0].len, 180u);
  EXPECT_EQ(result.fragments[1].len, 1u);
  ExpectPayload(result.fragments[0], frame, 0u);
  ExpectPayload(result.fragments[1], frame, 180u);
}

void TestMaximumFrame()
{
  MavlinkRtcmEncoder encoder;
  const auto frame = MakeFrame(MavlinkRtcmEncoder::kMaxFrameSize);
  const auto result = encoder.Encode(frame);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.fragments.size(), 4u);
  for (std::size_t index = 0u; index < result.fragments.size(); ++index)
  {
    EXPECT_EQ(result.fragments[index].flags,
              static_cast<std::uint8_t>(0x01u | (static_cast<std::uint8_t>(index) << 1u)));
    EXPECT_EQ(result.fragments[index].len, 180u);
    ExpectPayload(result.fragments[index], frame, index * 180u);
  }
}

void TestRejectedFramesDoNotConsumeSequence()
{
  MavlinkRtcmEncoder encoder;

  const auto empty = encoder.Encode({});
  EXPECT_EQ(empty.status, MavlinkRtcmEncodeStatus::kEmptyFrame);
  EXPECT_TRUE(empty.fragments.empty());

  const auto oversized = encoder.Encode(MakeFrame(MavlinkRtcmEncoder::kMaxFrameSize + 1u));
  EXPECT_EQ(oversized.status, MavlinkRtcmEncodeStatus::kFrameTooLarge);
  EXPECT_TRUE(oversized.fragments.empty());

  const auto valid = encoder.Encode(MakeFrame(1u));
  EXPECT_TRUE(valid.ok());
  EXPECT_EQ(valid.fragments[0].flags, 0u);
}

void TestSequenceWrapsAtFiveBits()
{
  MavlinkRtcmEncoder encoder;
  for (std::uint32_t sequence = 0u; sequence < 34u; ++sequence)
  {
    const auto result = encoder.Encode(MakeFrame(1u));
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.fragments.size(), 1u);
    EXPECT_EQ(result.fragments[0].flags,
              static_cast<std::uint8_t>((sequence & 0x1Fu) << 3u));
  }
}

}  // namespace

int main()
{
  TestSingleFragment();
  TestExactSingleFragmentLimit();
  TestFragmentationAndFlags();
  TestMaximumFrame();
  TestRejectedFramesDoNotConsumeSequence();
  TestSequenceWrapsAtFiveBits();

  if (failures != 0)
  {
    std::cerr << failures << " MAVLink RTCM encoder checks failed\n";
    return 1;
  }
  return 0;
}
