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
  // Exposed UART set with no camera module attached: excludes onboard SD, RGB, USB,
  // flash/PSRAM, strapping and camera power-enable. Debug console/JTAG uses USB.
  for (const int pin : {1, 2, 15, 16, 17, 18, 38, 39, 40, 41, 42, 43, 44, 47, 48})
    pins |= UINT64_C(1) << pin;
  return pins;
}
}  // namespace ug::embedded
