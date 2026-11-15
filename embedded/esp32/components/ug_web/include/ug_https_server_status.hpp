// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_http_server.h"
#include "ug_https_diagnostics.hpp"
namespace ug::embedded
{
inline HttpsStartupReason HttpsServerFailureReason(esp_err_t error, bool registering_routes)
{
  if (error == ESP_OK)
    return HttpsStartupReason::kReady;
  if (error == ESP_ERR_NO_MEM || error == ESP_ERR_HTTPD_ALLOC_MEM)
    return HttpsStartupReason::kNoMemory;
  return registering_routes ? HttpsStartupReason::kRouteRegistrationFailed
                            : HttpsStartupReason::kServerStartFailed;
}
}  // namespace ug::embedded
