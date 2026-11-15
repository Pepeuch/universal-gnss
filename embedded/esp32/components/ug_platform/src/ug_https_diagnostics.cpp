// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_https_diagnostics.hpp"
#include <mutex>
namespace ug::embedded
{
namespace
{
std::mutex guard;
HttpsStartupStatus status;
}  // namespace
HttpsStartupStatus LastHttpsStartupStatus()
{
  std::lock_guard<std::mutex> lock(guard);
  return status;
}
std::int32_t RecordHttpsStartupStatus(HttpsStartupReason reason, std::int32_t sdk_error)
{
  std::lock_guard<std::mutex> lock(guard);
  status = {reason, sdk_error};
  return static_cast<std::int32_t>(reason);
}
}  // namespace ug::embedded
