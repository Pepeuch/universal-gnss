// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_tls_bootstrap.hpp"
#include <array>
#include <cassert>
#include <string>
using namespace ug::embedded;
namespace
{
constexpr HttpsStartupStatus ready{HttpsStartupReason::kReady, 0};
constexpr HttpsStartupStatus missing{HttpsStartupReason::kIdentityMissing, 4354};
struct Backend : TlsBootstrapBackend
{
  HttpsStartupStatus stored{missing}, generation{ready}, persistence{ready};
  unsigned loads{}, generates{}, commits{};
  bool lost_power{}, corrupt_readback{};
  std::string identity, candidate;
  // Model unrelated namespaces to prove the bootstrap never resets them.
  const std::array<std::string, 7> settings{
      "gnss", "ntrip", "ethernet", "wifi", "espnow", "admin", "tls-client"};
  HttpsStartupStatus Load() override
  {
    ++loads;
    if (commits && corrupt_readback)
      return {HttpsStartupReason::kIdentityInvalid, 260};
    return stored;
  }
  HttpsStartupStatus Generate() override
  {
    ++generates;
    candidate = "unique-identity-" + std::to_string(generates);
    return generation;
  }
  HttpsStartupStatus Persist() override
  {
    ++commits;
    if (!persistence.ok())
      return persistence;
    if (lost_power)
      return {HttpsStartupReason::kIdentityPersistenceFailed, -1};
    identity = candidate;
    stored = ready;
    return ready;
  }
};
}  // namespace
int main()
{
  Backend fresh;
  const auto initial_settings = fresh.settings;
  auto result = BootstrapTls(fresh);
  assert(result.status.ok() && result.generated && !result.recovered);
  assert(fresh.generates == 1 && fresh.commits == 1 && fresh.loads == 2);
  assert(fresh.settings == initial_settings);
  const auto retained = fresh.identity;
  result = BootstrapTls(fresh);  // Cold boot / update: valid identity is retained.
  assert(result.status.ok() && !result.generated && fresh.generates == 1);
  assert(fresh.identity == retained && fresh.settings == initial_settings);
  Backend corrupt;
  corrupt.stored = {HttpsStartupReason::kIdentityInvalid, 260};
  result = BootstrapTls(corrupt);
  assert(result.status.ok() && result.recovered && corrupt.commits == 1);
  for (const auto reason : {HttpsStartupReason::kIdentityUnreadable,
                            HttpsStartupReason::kNoMemory,
                            HttpsStartupReason::kCryptoUnavailable})
  {
    Backend blocked;
    blocked.stored = {reason, -37};
    result = BootstrapTls(blocked);
    assert(result.status.reason == reason && result.status.sdk_error == -37);
    assert(!blocked.generates && !blocked.commits);
  }
  for (const auto reason :
       {HttpsStartupReason::kNoMemory, HttpsStartupReason::kIdentityGenerationFailed})
  {
    Backend failed;
    failed.generation = {reason, -39};
    result = BootstrapTls(failed);
    assert(result.status.reason == reason && std::string(result.stage) == "generate");
    assert(!failed.commits && failed.identity.empty());
  }
  Backend interrupted;
  interrupted.lost_power = true;
  result = BootstrapTls(interrupted);
  assert(!result.status.ok() && std::string(result.stage) == "persist");
  assert(interrupted.identity.empty() && interrupted.settings == initial_settings);
  interrupted.lost_power = false;
  result = BootstrapTls(interrupted);
  assert(result.status.ok() && interrupted.generates == 2);
  Backend readback;
  readback.corrupt_readback = true;
  result = BootstrapTls(readback);
  assert(!result.status.ok() && std::string(result.stage) == "readback");
  Backend persistence;
  persistence.persistence = {HttpsStartupReason::kIdentityPersistenceFailed, -41};
  result = BootstrapTls(persistence);
  assert(result.status.sdk_error == -41 && persistence.identity.empty());
}
