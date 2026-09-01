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

struct NtripSourceIdentity
{
  std::string host{};
  std::uint16_t port{0u};
  std::string mountpoint{};
};

inline bool operator==(const NtripSourceIdentity& lhs, const NtripSourceIdentity& rhs)
{
  return lhs.host == rhs.host && lhs.port == rhs.port && lhs.mountpoint == rhs.mountpoint;
}

inline bool operator!=(const NtripSourceIdentity& lhs, const NtripSourceIdentity& rhs)
{
  return !(lhs == rhs);
}

struct NtripConfig
{
  std::string host{};
  std::uint16_t port{2101u};
  std::string mountpoint{};
  std::string username{};
  std::string password{};
  std::string user_agent{kDefaultNtripUserAgent};
  NtripVersion version{NtripVersion::kV2};
  bool tls_enabled{false};
  // Intended only for deterministic local TLS fixtures. Production callers
  // must retain certificate and host verification.
  bool tls_verify_peer{true};
  bool send_gga{false};
  std::uint32_t gga_interval_s{10u};
  // Zero disables the corresponding liveness deadline. Defaults match the
  // existing 30-second correction-observation horizon rather than forwarding
  // activity diagnostics, which are a separate concern.
  std::uint32_t first_rtcm_frame_timeout_ms{30000u};
  std::uint32_t rtcm_frame_timeout_ms{30000u};
  // A response plus one frame demonstrates progress, but two complete valid
  // frames avoid resetting backoff for connect-one-frame-drop loops.
  std::uint32_t operational_min_valid_rtcm_frames{2u};
  NtripReconnectPolicy reconnect_policy{};
};

NtripSourceIdentity BuildNtripSourceIdentity(const NtripConfig& config);

}  // namespace universal_gnss_ntrip
