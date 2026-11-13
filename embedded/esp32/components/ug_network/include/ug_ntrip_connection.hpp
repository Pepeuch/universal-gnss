// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss_ntrip/ntrip_client.hpp"
#include <array>
#include <functional>
namespace ug::embedded
{
// The same connection supervisor is used by BASE and GNSS-free relay. It
// delegates HTTP, GGA, correction observation and backoff to the actual UG client.
class NtripConnection
{
public:
  using FrameSink = std::function<void(const universal_gnss_protocols::RtcmFrame&)>;
  using SessionBoundary = std::function<void(bool)>;
  NtripConnection(FrameSink sink, SessionBoundary boundary);
  bool Configure(const universal_gnss_ntrip::NtripConfig& config,
                 bool enabled,
                 std::function<bool()> cancelled = {});
  void Tick(std::int64_t now_ns, const universal_gnss::GnssRuntimeState* gga_state = nullptr);
  void Stop();
  const universal_gnss_ntrip::NtripClient& client() const
  {
    return client_;
  }
  bool enabled() const
  {
    return enabled_;
  }

private:
  void EndSession();
  universal_gnss_ntrip::NtripClient client_;
  FrameSink sink_;
  SessionBoundary boundary_;
  bool enabled_{false}, attempted_{false}, session_open_{false};
  std::array<std::uint8_t, 512> bytes_{};
  std::vector<universal_gnss_protocols::RtcmFrame> frames_{};
};
}  // namespace ug::embedded
