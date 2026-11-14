#include "ug_rtcm.h"

#include "universal_gnss_protocols/rtcm_crc24q.hpp"

bool ug_rtcm_crc24q(const uint8_t* data, size_t size, uint32_t* crc)
{
  if (crc == nullptr || (data == nullptr && size != 0))
  {
    return false;
  }
  *crc = universal_gnss_protocols::ComputeRtcmCrc24Q(data, size);
  return true;
}

bool ug_rtcm_frame_valid(const uint8_t* data, size_t size)
{
  if (data == nullptr || size < 6 || size > 1029 || data[0] != 0xD3 || (data[1] & 0xFC) != 0)
  {
    return false;
  }
  const size_t payload_size = (static_cast<size_t>(data[1] & 3) << 8) | data[2];
  if (size != payload_size + 6)
  {
    return false;
  }
  const uint32_t expected = (static_cast<uint32_t>(data[size - 3]) << 16) |
                            (static_cast<uint32_t>(data[size - 2]) << 8) | data[size - 1];
  return universal_gnss_protocols::ValidateRtcmCrc24Q(data, size - 3, expected);
}
