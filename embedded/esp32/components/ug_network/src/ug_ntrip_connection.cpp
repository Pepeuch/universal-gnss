// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_ntrip_connection.hpp"
#include <algorithm>
namespace ug::embedded
{
namespace
{
bool SafeText(const std::string& text, const std::size_t limit)
{
  return text.size() <= limit && std::none_of(text.begin(), text.end(), [](const unsigned char c) {
           return c < 32 || c == 127;
         });
}
}  // namespace
NtripConnection::NtripConnection(FrameSink sink, SessionBoundary boundary)
    : sink_(std::move(sink)), boundary_(std::move(boundary))
{
}
bool NtripConnection::Configure(const universal_gnss_ntrip::NtripConfig& config,
                                const bool enabled,
                                std::function<bool()> cancelled)
{
  if (enabled &&
      (config.host.empty() || !config.port || !SafeText(config.host, 253) ||
       !SafeText(config.mountpoint, 128) || !SafeText(config.username, 128) ||
       !SafeText(config.password, 128) || !SafeText(config.user_agent, 128) ||
       config.tls_ca_file.size() > 128 || config.tls_client_certificate_file.size() > 128 ||
       config.tls_client_private_key_file.size() > 128 ||
       (config.tls_enabled && !config.tls_verify_peer)))
    return false;
  Stop();
  client_.set_config(config);
  universal_gnss_transport::TcpClientConfig transport;
  transport.connect_timeout_ms = 5000;
  transport.read_timeout_ms = 20;
  transport.write_timeout_ms = 1000;
  transport.tcp_nodelay = true;
  transport.connect_cancelled = std::move(cancelled);
  client_.set_tcp_config(transport);
  enabled_ = enabled;
  attempted_ = false;
  trace_.Emit(enabled ? NtripTraceKind::kEnabled : NtripTraceKind::kDisabled);
  return true;
}
void NtripConnection::EndSession()
{
  if (session_open_)
  {
    session_open_ = false;
    if (boundary_)
      boundary_(false);
  }
}
void NtripConnection::Stop()
{
  const bool was_enabled = enabled_;
  client_.Disconnect();
  EndSession();
  enabled_ = false;
  frames_.clear();
  if (was_enabled)
    trace_.Emit(NtripTraceKind::kStopped);
}
void NtripConnection::Tick(const std::int64_t now_ns, const universal_gnss::GnssRuntimeState* state)
{
  using namespace universal_gnss_ntrip;
  if (!enabled_ || now_ns < 0)
    return;
  if (!client_.IsConnected())
  {
    if (session_open_)
      trace_.Emit(NtripTraceKind::kFailure,
                  NtripTraceError::kClient,
                  static_cast<std::uint8_t>(client_.metrics().last_error));
    EndSession();
    if (attempted_ &&
        !client_.config().reconnect_policy.ShouldReconnect(client_.reconnect_state(), now_ns))
      return;
    attempted_ = true;
    trace_.Emit(NtripTraceKind::kConnectAttempt);
    const auto error = client_.Connect(now_ns);
    if (error != NtripClientError::kNone)
    {
      trace_.Emit(
          NtripTraceKind::kFailure, NtripTraceError::kClient, static_cast<std::uint8_t>(error));
      return;
    }
    trace_.Emit(NtripTraceKind::kConnected);
    session_open_ = true;
    if (boundary_)
      boundary_(true);
    // Never stamp a read with a pre-connect time: Connect can consume seconds.
    return;
  }
  if (!client_.metrics().request_sent)
  {
    const auto error = client_.SendRequest(now_ns);
    if (error != NtripClientError::kNone)
    {
      trace_.Emit(
          NtripTraceKind::kFailure, NtripTraceError::kClient, static_cast<std::uint8_t>(error));
      EndSession();
    }
    else
      trace_.Emit(NtripTraceKind::kRequestSent);
    return;
  }
  frames_.clear();
  const bool accepted = client_.correction_flow_state().response_accepted;
  const auto read = client_.Read(bytes_.data(), bytes_.size(), now_ns, &frames_);
  if (!accepted && client_.correction_flow_state().response_accepted)
    trace_.Emit(NtripTraceKind::kAccepted);
  for (const auto& frame : frames_)
    if (frame.checksum_status == universal_gnss_protocols::ChecksumStatus::kValid && sink_)
      sink_(frame);
  if (read.client_error != NtripClientError::kNone || !client_.IsConnected())
  {
    trace_.Emit(NtripTraceKind::kFailure,
                NtripTraceError::kClient,
                static_cast<std::uint8_t>(read.client_error != NtripClientError::kNone
                                              ? read.client_error
                                              : NtripClientError::kDisconnected));
    EndSession();
    return;
  }
  if (state && client_.config().send_gga)
    (void)client_.MaybeInjectGga(*state, now_ns);
}
}  // namespace ug::embedded
