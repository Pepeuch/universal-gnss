#include "universal_gnss_protocols/rtcm_crc24q.hpp"

namespace universal_gnss_protocols
{

std::uint32_t ComputeRtcmCrc24Q(const std::uint8_t* data, std::size_t size)
{
  constexpr std::uint32_t kPolynomial = 0x1864CFBu;

  std::uint32_t crc = 0;
  for (std::size_t i = 0; i < size; ++i)
  {
    crc ^= static_cast<std::uint32_t>(data[i]) << 16;
    for (int bit = 0; bit < 8; ++bit)
    {
      crc <<= 1;
      if ((crc & 0x1000000u) != 0u)
      {
        crc ^= kPolynomial;
      }
      crc &= 0xFFFFFFu;
    }
  }

  return crc;
}

bool ValidateRtcmCrc24Q(const std::uint8_t* data,
                        std::size_t size,
                        std::uint32_t expected_crc24q)
{
  return ComputeRtcmCrc24Q(data, size) == (expected_crc24q & 0xFFFFFFu);
}

}  // namespace universal_gnss_protocols
