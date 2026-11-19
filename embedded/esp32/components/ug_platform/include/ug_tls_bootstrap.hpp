// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_https_diagnostics.hpp"
namespace ug::embedded
{
// Policy shared by the real SDK adapter and deterministic fault-injection tests.
// The backend owns temporary secrets and wipes them on every exit.
class TlsBootstrapBackend
{
public:
  virtual ~TlsBootstrapBackend() = default;
  virtual HttpsStartupStatus Load() = 0;
  virtual HttpsStartupStatus Generate() = 0;
  virtual HttpsStartupStatus Persist() = 0;
};
struct TlsBootstrapResult
{
  HttpsStartupStatus status;
  const char* stage{"load"};
  bool generated{}, recovered{};
};
inline TlsBootstrapResult BootstrapTls(TlsBootstrapBackend& backend)
{
  TlsBootstrapResult result{backend.Load()};
  if (result.status.ok())
    return result;
  // Never mistake transient read/crypto/OOM errors for missing/corrupt storage.
  if (result.status.reason != HttpsStartupReason::kIdentityMissing &&
      result.status.reason != HttpsStartupReason::kIdentityInvalid)
    return result;
  result.recovered = result.status.reason == HttpsStartupReason::kIdentityInvalid;
  result.stage = "generate";
  result.status = backend.Generate();
  if (!result.status.ok())
    return result;
  result.generated = true;
  result.stage = "persist";
  result.status = backend.Persist();
  if (!result.status.ok())
    return result;
  result.stage = "readback";
  result.status = backend.Load();
  if (result.status.ok())
    result.stage = "ready";
  return result;
}
// Startup only, before Wi-Fi/ADC owners run. No NVS/configuration erasure.
HttpsStartupStatus PrepareWebIdentityAtBoot();
HttpsStartupStatus WebIdentityBootStatus();
}  // namespace ug::embedded
