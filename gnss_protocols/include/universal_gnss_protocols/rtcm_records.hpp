#pragma once

#include <cstdint>

namespace universal_gnss_protocols
{

enum class RtcmConstellation : std::uint8_t
{
  kUnknown = 0,
  kGps = 1,
  kGlonass = 2,
  kGalileo = 3,
  kSbas = 4,
  kQzss = 5,
  kBeiDou = 6,
  kNavIc = 7,
};

struct RtcmMessageInfo
{
  std::uint16_t message_type{0};
  bool is_station_arp{false};
  bool is_glonass_bias{false};
  bool is_msm{false};
  RtcmConstellation msm_constellation{RtcmConstellation::kUnknown};
};

}  // namespace universal_gnss_protocols
