#pragma once

#include <cstddef>
#include <cstdint>

namespace universal_gnss_protocols
{

std::uint32_t ComputeRtcmCrc24Q(const std::uint8_t* data, std::size_t size);

bool ValidateRtcmCrc24Q(const std::uint8_t* data,
                        std::size_t size,
                        std::uint32_t expected_crc24q);

}  // namespace universal_gnss_protocols
