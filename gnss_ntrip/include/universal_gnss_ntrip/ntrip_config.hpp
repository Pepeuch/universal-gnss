#pragma once

#include <cstdint>
#include <string>

#include "universal_gnss_ntrip/ntrip_reconnect_policy.hpp"

namespace universal_gnss_ntrip
{

enum class NtripVersion : std::uint8_t
{
  kV1 = 1,
  kV2 = 2,
};

inline constexpr const char* kDefaultNtripUserAgent = "universal-gnss";

struct NtripConfig
{
  std::string host{};
  std::uint16_t port{2101u};
  std::string mountpoint{};
  std::string username{};
  std::string password{};
  std::string user_agent{kDefaultNtripUserAgent};
  NtripVersion version{NtripVersion::kV2};
  bool send_gga{false};
  std::uint32_t gga_interval_s{10u};
  NtripReconnectPolicy reconnect_policy{};
};

}  // namespace universal_gnss_ntrip
