#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace universal_gnss_mavros
{

struct MavlinkRtcmFragment
{
  static constexpr std::size_t kPayloadCapacity = 180u;

  std::uint8_t flags{0u};
  std::uint8_t len{0u};
  std::array<std::uint8_t, kPayloadCapacity> data{};
};

enum class MavlinkRtcmEncodeStatus : std::uint8_t
{
  kOk = 0,
  kEmptyFrame,
  kFrameTooLarge,
};

struct MavlinkRtcmEncodeResult
{
  MavlinkRtcmEncodeStatus status{MavlinkRtcmEncodeStatus::kOk};
  std::vector<MavlinkRtcmFragment> fragments{};

  bool ok() const { return status == MavlinkRtcmEncodeStatus::kOk; }
};

class MavlinkRtcmEncoder
{
public:
  static constexpr std::size_t kMaxFragments = 4u;
  static constexpr std::size_t kMaxFrameSize =
      MavlinkRtcmFragment::kPayloadCapacity * kMaxFragments;

  MavlinkRtcmEncodeResult Encode(const std::vector<std::uint8_t>& frame);

private:
  std::atomic_uint sequence_{0u};
};

}  // namespace universal_gnss_mavros
