// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstdint>
namespace ug::embedded
{
enum class NtripTraceKind : std::uint8_t
{
  kEnabled,
  kDisabled,
  kConnectAttempt,
  kConnected,
  kRequestSent,
  kAccepted,
  kFailure,
  kStopped
};
enum class NtripTraceError : std::uint8_t
{
  kNone,
  kClient,
  kConnect,
  kWrite,
  kRead,
  kProtocol,
  kTimeout,
  kCancelled
};
inline const char* NtripTraceName(NtripTraceKind kind)
{
  switch (kind)
  {
    case NtripTraceKind::kEnabled:
      return "enabled";
    case NtripTraceKind::kDisabled:
      return "disabled";
    case NtripTraceKind::kConnectAttempt:
      return "connect_attempt";
    case NtripTraceKind::kConnected:
      return "transport_connected";
    case NtripTraceKind::kRequestSent:
      return "request_sent";
    case NtripTraceKind::kAccepted:
      return "response_accepted";
    case NtripTraceKind::kFailure:
      return "failure";
    case NtripTraceKind::kStopped:
      return "stopped";
  }
  return "invalid";
}
inline const char* NtripTraceErrorName(NtripTraceError error)
{
  switch (error)
  {
    case NtripTraceError::kNone:
      return "none";
    case NtripTraceError::kClient:
      return "ug_client_error";
    case NtripTraceError::kConnect:
      return "connect";
    case NtripTraceError::kWrite:
      return "write";
    case NtripTraceError::kRead:
      return "read";
    case NtripTraceError::kProtocol:
      return "response_protocol";
    case NtripTraceError::kTimeout:
      return "response_timeout";
    case NtripTraceError::kCancelled:
      return "cancelled";
  }
  return "invalid";
}
// Single-owner synchronous observer. No allocation, raw text or transport ownership.
// The recipient timestamps receipt after blocking operations, in its own clock domain.
struct NtripTraceSink
{
  using Callback = void (*)(NtripTraceKind, NtripTraceError, std::uint8_t, void*);
  Callback callback{};
  void* context{};
  void Emit(NtripTraceKind kind,
            NtripTraceError error = NtripTraceError::kNone,
            std::uint8_t client_error = 0) const
  {
    if (callback)
      callback(kind, error, client_error, context);
  }
};
}  // namespace ug::embedded
