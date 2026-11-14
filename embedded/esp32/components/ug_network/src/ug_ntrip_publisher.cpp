// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_ntrip_publisher.hpp"
#include "ug_rtcm.h"
#include "universal_gnss_ntrip/ntrip_request.hpp"
#include <algorithm>
#include <chrono>
#include <string_view>
namespace ug::embedded
{
namespace
{
bool Token(const std::string& s, std::size_t max, bool empty = false)
{
  return (empty || !s.empty()) && s.size() <= max &&
         std::none_of(s.begin(), s.end(), [](unsigned char c) { return c <= 32 || c == 127; });
}
}  // namespace
bool NtripPublisher::Configure(const universal_gnss_ntrip::NtripConfig& config,
                               bool enabled,
                               std::function<bool()> cancelled)
{
  if (enabled &&
      (!Token(config.host, 253) || !config.port || !Token(config.mountpoint, 128) ||
       !Token(config.password, 128) || !Token(config.user_agent, 128) || !config.tls_verify_peer))
    return false;
  Stop();
  config_ = config;
  cancelled_ = std::move(cancelled);
  tcp_ = {};
  tcp_.host = config.host;
  tcp_.port = config.port;
  tcp_.connect_timeout_ms = 5000;
  tcp_.read_timeout_ms = 20;
  tcp_.write_timeout_ms = 1000;
  tcp_.tcp_nodelay = true;
  tcp_.tls_enabled = config.tls_enabled;
  tcp_.tls_verify_peer = config.tls_verify_peer;
  tcp_.tls_ca_file = config.tls_ca_file;
  tcp_.tls_client_certificate_file = config.tls_client_certificate_file;
  tcp_.tls_client_private_key_file = config.tls_client_private_key_file;
  tcp_.connect_cancelled = cancelled_;
  reconnect_.Reset();
  attempted_ = false;
  session_frames_ = 0;
  if (enabled)
    state_ = State::kConnect;
  trace_.Emit(enabled ? NtripTraceKind::kEnabled : NtripTraceKind::kDisabled);
  return true;
}
void NtripPublisher::Stop()
{
  const bool was_enabled = state_ != State::kStopped;
  transport_.Close();
  state_ = State::kStopped;
  response_size_ = 0;
  if (was_enabled)
    trace_.Emit(NtripTraceKind::kStopped);
}
void NtripPublisher::Fail(const std::int64_t now_ns, NtripTraceError error)
{
  transport_.Close();
  ++metrics_.failures;
  state_ = State::kConnect;
  response_size_ = 0;
  config_.reconnect_policy.OnFailure(reconnect_, now_ns);
  trace_.Emit(NtripTraceKind::kFailure, error);
}
bool NtripPublisher::WriteAll(const std::uint8_t* data, std::size_t size)
{
  std::size_t sent = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  // Write does one bounded progress attempt. Never spin on a timeout/zero write.
  while (sent < size)
  {
    if ((cancelled_ && cancelled_()) || std::chrono::steady_clock::now() >= deadline)
      return false;
    const auto out = transport_.Write(data + sent, size - sent);
    if (out.status != universal_gnss_transport::TransportStatus::kOk || !out.bytes_written ||
        out.bytes_written > size - sent)
      return false;
    sent += out.bytes_written;
  }
  return true;
}
void NtripPublisher::Tick(const std::int64_t now_ns)
{
  if (state_ == State::kStopped || now_ns < 0)
    return;
  if (cancelled_ && cancelled_())
  {
    Fail(now_ns, NtripTraceError::kCancelled);
    return;
  }
  if (state_ == State::kConnect)
  {
    if (attempted_ && !config_.reconnect_policy.ShouldReconnect(reconnect_, now_ns))
      return;
    attempted_ = true;
    session_frames_ = 0;
    trace_.Emit(NtripTraceKind::kConnectAttempt);
    if (transport_.Open(tcp_) != universal_gnss_transport::TransportError::kNone)
    {
      Fail(now_ns, NtripTraceError::kConnect);
      return;
    }
    ++metrics_.connects;
    state_ = State::kRequest;
    trace_.Emit(NtripTraceKind::kConnected);
    return;
  }
  if (state_ == State::kRequest)
  {
    // Historical operational publisher uses SOURCE v1 (the old version selector did not change it).
    const auto request = "SOURCE " + config_.password + " " +
                         universal_gnss_ntrip::NormalizeMountpointPath(config_.mountpoint) +
                         "\r\nSource-Agent: NTRIP " + config_.user_agent + "\r\n\r\n";
    if (!WriteAll(reinterpret_cast<const std::uint8_t*>(request.data()), request.size()))
    {
      Fail(now_ns, NtripTraceError::kWrite);
      return;
    }
    response_started_ = now_ns;
    state_ = State::kResponse;
    trace_.Emit(NtripTraceKind::kRequestSent);
    return;
  }
  if (state_ == State::kResponse)
  {
    if (now_ns < response_started_ || now_ns - response_started_ > 5000000000LL ||
        response_size_ >= response_.size() - 1)
    {
      Fail(now_ns,
           now_ns < response_started_ || now_ns - response_started_ > 5000000000LL
               ? NtripTraceError::kTimeout
               : NtripTraceError::kProtocol);
      return;
    }
    std::uint8_t bytes[128];
    const auto read = transport_.Read(bytes, sizeof(bytes));
    if (read.status == universal_gnss_transport::TransportStatus::kOk && !read.bytes_read)
      return;
    if (read.status != universal_gnss_transport::TransportStatus::kOk ||
        read.bytes_read > sizeof(bytes))
    {
      Fail(now_ns, NtripTraceError::kRead);
      return;
    }
    for (std::size_t i = 0; i < read.bytes_read; ++i)
    {
      if (response_size_ == response_.size() - 1 || bytes[i] == 0)
      {
        Fail(now_ns, NtripTraceError::kProtocol);
        return;
      }
      response_[response_size_++] = bytes[i];
      if (bytes[i] == '\n')
      {
        const std::string_view line(response_.data(), response_size_);
        // SOURCE-v1 acknowledges with ICY; HTTP upload/chunking is not silently invented.
        if (line != "ICY 200 OK\r\n" && line != "ICY 200 OK\n")
        {
          Fail(now_ns, NtripTraceError::kProtocol);
          return;
        }
        state_ = State::kStreaming;
        trace_.Emit(NtripTraceKind::kAccepted);
        return;
      }
    }
  }
  else if (state_ == State::kStreaming)
  {
    // Drain server diagnostics/observe orderly close even when corrections are idle.
    std::uint8_t bytes[64];
    const auto read = transport_.Read(bytes, sizeof(bytes));
    if (read.status != universal_gnss_transport::TransportStatus::kOk)
      Fail(now_ns, NtripTraceError::kRead);
  }
}
bool NtripPublisher::Send(const Gateway::Frame& frame, const std::int64_t now_ns)
{
  if (!streaming())
    return false;
  if (now_ns < frame.receipt_ns || now_ns - frame.receipt_ns > 3000000000LL)
  {
    ++metrics_.expired;
    return false;
  }
  if (frame.size > frame.bytes.size() || !ug_rtcm_frame_valid(frame.bytes.data(), frame.size))
    return false;
  if (!WriteAll(frame.bytes.data(), frame.size))
  {
    Fail(now_ns, NtripTraceError::kWrite);
    return false;
  }
  ++metrics_.frames;
  metrics_.bytes += frame.size;
  if (++session_frames_ == 2)
    config_.reconnect_policy.OnSuccess(reconnect_, now_ns);
  return true;
}
}  // namespace ug::embedded
