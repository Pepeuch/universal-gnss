#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "universal_gnss_protocols/protocol_records.hpp"

namespace universal_gnss_ntrip
{

struct LocalRtcmSourceIdentity
{
  std::string source_id{};
  std::uint64_t incarnation{0u};
};

struct LocalRtcmCasterConfig
{
  std::string bind_host{"127.0.0.1"};
  std::uint16_t port{0u};
  std::string mountpoint{"RTCM3"};
  std::size_t client_buffer_bytes{65536u};
};

struct LocalRtcmCasterPolicy
{
  // Zero preserves the historical unlimited/disabled host policy. Firmware
  // explicitly supplies bounded limits and source-boundary disconnection.
  std::size_t max_clients{0u};
  std::size_t max_accepts_per_poll{0u};
  std::uint32_t request_timeout_ms{0u};
  bool disconnect_clients_on_source_change{false};
  std::string username{}, password{};  // Empty pair preserves anonymous access.
};

struct LocalRtcmCasterMetrics
{
  std::uint64_t accepted_clients{0u};
  std::uint64_t rejected_requests{0u};
  std::uint64_t slow_client_disconnects{0u};
  std::uint64_t served_frames{0u};
};

// Synchronous local NTRIP caster on Linux or ESP-IDF. Poll() owns accept/request/write
// progress; PublishFrame() never waits for a client.
class LocalRtcmCaster
{
public:
  LocalRtcmCaster();
  ~LocalRtcmCaster();
  LocalRtcmCaster(const LocalRtcmCaster&) = delete;
  LocalRtcmCaster& operator=(const LocalRtcmCaster&) = delete;

  // Opt-in policy; may be replaced only while stopped. Existing config ABI is unchanged.
  bool SetPolicy(LocalRtcmCasterPolicy policy);
  bool Start(LocalRtcmCasterConfig config);
  void Stop();
  bool running() const;
  std::uint16_t port() const;

  // Activating a source always starts a distinct cache ownership incarnation.
  bool ActivateSource(LocalRtcmSourceIdentity source);
  void EndSource();
  std::optional<LocalRtcmSourceIdentity> active_source() const;

  // Accepts only CRC-valid complete RTCM frames from the active incarnation.
  // 1005/1006 are retained only for that incarnation; all other frames are live.
  bool PublishFrame(const universal_gnss_protocols::RtcmFrame& frame);
  void Poll();

  const LocalRtcmCasterMetrics& metrics() const;

private:
  struct Impl;
  Impl* impl_;
};

}  // namespace universal_gnss_ntrip
