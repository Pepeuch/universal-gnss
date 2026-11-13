// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string_view>
namespace ug::embedded
{
// Candidate UART GPIOs for this native board profile. This is resource exclusion,
// not proof of physical wiring or of receiver voltage compatibility.
inline std::uint64_t GnssPinsForBoard(std::string_view board)
{
  if (board != "waveshare-s3-eth")
    return 0;
  std::uint64_t pins = 0;
  for (const int pin :
       {1, 2, 4, 5, 6, 7, 8, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 43, 44, 47, 48})
    pins |= UINT64_C(1) << pin;
  return pins;
}
}  // namespace ug::embedded
