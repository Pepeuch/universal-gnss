#pragma once

#include <cstddef>
#include <cstdint>

namespace universal_gnss_protocols
{

struct UbxChecksum
{
  std::uint8_t ck_a{0};
  std::uint8_t ck_b{0};
};

UbxChecksum ComputeUbxChecksum(const std::uint8_t* data, std::size_t size);

bool ValidateUbxChecksum(const std::uint8_t* data,
                         std::size_t size,
                         const UbxChecksum& expected_checksum);

}  // namespace universal_gnss_protocols
