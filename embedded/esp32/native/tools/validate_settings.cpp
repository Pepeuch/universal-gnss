// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_board.hpp"
#include "ug_settings.hpp"
#include <array>
#include <iostream>
#include <string_view>
int main(int argc, char** argv)
{
  using namespace ug::embedded;
  if (argc != 3)
    return 2;
  const std::string_view board = argv[1], role = argv[2];
  if ((board != "waveshare-s3-eth" && board != "supermini-c3") ||
      (role != "gateway" && role != "relay-ntrip") ||
      (board == "supermini-c3" && role != "relay-ntrip"))
    return 2;
  const bool s3 = board == "waveshare-s3-eth";
  Capabilities caps{
      s3 && role == "gateway", s3, false, s3 ? 5u : 1u, std::string(board), std::string(role)};
  caps.gnss_pin_mask = GnssPinsForBoard(board);
  std::array<char, 8193> bytes{};
  std::cin.read(bytes.data(), bytes.size());
  const auto size = std::cin.gcount();
  Settings candidate;
  std::string error;
  if (size <= 0 || size > 8192 ||
      !DecodeSettings({bytes.data(), static_cast<std::size_t>(size)},
                      DefaultSettings(caps),
                      caps,
                      candidate,
                      error))
  {
    std::cerr << "UG_SCHEMA_REJECTED\n";  // Never echo configuration or credential values.
    return 1;
  }
  std::cout << "UG_SCHEMA_VALID\n";
  return 0;
}
