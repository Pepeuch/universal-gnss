// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_gateway.hpp"
#include "ug_ntrip_trace.hpp"
#include "universal_gnss_ntrip/ntrip_config.hpp"
#include "universal_gnss_transport/tcp_client_transport.hpp"
namespace ug::embedded
{
// Single-owner SOURCE-v1 uploader, sharing UG transport/TLS and reconnect policy.
// Native UG has no upstream uploader; this replaces the historical socket implementation.
class NtripPublisher
{
public:
  struct Metrics
  {
    std::uint64_t connects{}, frames{}, bytes{}, failures{}, expired{};
  };
  bool Configure(const universal_gnss_ntrip::NtripConfig&,
                 bool enabled,
                 std::function<bool()> cancelled = {});
  void Tick(std::int64_t now_ns);
  bool Send(const Gateway::Frame&, std::int64_t now_ns);
  void Stop();
  void SetTrace(NtripTraceSink sink)
  {
    trace_ = sink;
  }
  bool streaming() const
  {
    return state_ == State::kStreaming;
  }
  const Metrics& metrics() const
  {
    return metrics_;
  }

private:
  enum class State
  {
    kStopped,
    kConnect,
    kRequest,
    kResponse,
    kStreaming
  };
  void Fail(std::int64_t now_ns, NtripTraceError error);
  NtripTraceSink trace_;
  bool WriteAll(const std::uint8_t*, std::size_t);
  State state_{State::kStopped};
  universal_gnss_transport::TcpClientTransport transport_;
  universal_gnss_transport::TcpClientConfig tcp_;
  universal_gnss_ntrip::NtripConfig config_;
  universal_gnss_ntrip::NtripReconnectState reconnect_;
  Metrics metrics_;
  bool attempted_{false};
  unsigned session_frames_{0};
  std::array<char, 1024> response_{};
  std::size_t response_size_{};
  std::int64_t response_started_{};
  std::function<bool()> cancelled_;
};
}  // namespace ug::embedded
