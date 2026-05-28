#pragma once

#include <cstdint>
#include <string>

namespace universal_gnss_ntrip
{

enum class NtripVersion : std::uint8_t
{
  kV1 = 1,
  kV2 = 2,
};

struct NtripReconnectBackoff
{
  std::uint32_t initial_delay_ms{1000u};
  std::uint32_t max_delay_ms{30000u};
  float multiplier{2.0f};
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
  NtripReconnectBackoff reconnect_backoff{};
};

}  // namespace universal_gnss_ntrip
