#include "universal_gnss_protocols/ubx_checksum.hpp"

namespace universal_gnss_protocols
{

UbxChecksum ComputeUbxChecksum(const std::uint8_t* data, std::size_t size)
{
  UbxChecksum checksum;
  for (std::size_t i = 0; i < size; ++i)
  {
    checksum.ck_a = static_cast<std::uint8_t>(checksum.ck_a + data[i]);
    checksum.ck_b = static_cast<std::uint8_t>(checksum.ck_b + checksum.ck_a);
  }
  return checksum;
}

bool ValidateUbxChecksum(const std::uint8_t* data,
                         std::size_t size,
                         const UbxChecksum& expected_checksum)
{
  const UbxChecksum computed = ComputeUbxChecksum(data, size);
  return computed.ck_a == expected_checksum.ck_a &&
         computed.ck_b == expected_checksum.ck_b;
}

}  // namespace universal_gnss_protocols
