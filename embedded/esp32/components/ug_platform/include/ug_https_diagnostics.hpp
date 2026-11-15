// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstdint>
namespace ug::embedded
{
// Stable local error codes also stored by the existing service-event ring.
// Only classifications and numeric SDK errors cross this boundary, never PEM.
enum class HttpsStartupReason : std::int32_t
{
  kReady = 0,
  kNotStarted = 0x6e00,
  kIdentityMissing,
  kIdentityUnreadable,
  kIdentityInvalid,
  kNoMemory,
  kCryptoUnavailable,
  kServerStartFailed,
  kRouteRegistrationFailed
};
constexpr const char* HttpsStartupReasonName(HttpsStartupReason reason)
{
  switch (reason)
  {
    case HttpsStartupReason::kReady:
      return "ready";
    case HttpsStartupReason::kNotStarted:
      return "not_started";
    case HttpsStartupReason::kIdentityMissing:
      return "identity_missing";
    case HttpsStartupReason::kIdentityUnreadable:
      return "identity_unreadable";
    case HttpsStartupReason::kIdentityInvalid:
      return "identity_invalid";
    case HttpsStartupReason::kNoMemory:
      return "no_memory";
    case HttpsStartupReason::kCryptoUnavailable:
      return "crypto_unavailable";
    case HttpsStartupReason::kServerStartFailed:
      return "server_start_failed";
    case HttpsStartupReason::kRouteRegistrationFailed:
      return "route_registration_failed";
  }
  return "unspecified";
}
struct HttpsStartupStatus
{
  HttpsStartupReason reason{HttpsStartupReason::kNotStarted};
  std::int32_t sdk_error{};
  bool ok() const
  {
    return reason == HttpsStartupReason::kReady;
  }
};
HttpsStartupStatus LastHttpsStartupStatus();
std::int32_t RecordHttpsStartupStatus(HttpsStartupReason reason, std::int32_t sdk_error);
}  // namespace ug::embedded
