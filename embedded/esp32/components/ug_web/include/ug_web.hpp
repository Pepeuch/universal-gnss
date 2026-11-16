// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
namespace ug::embedded
{
esp_err_t StartWebService();
bool WebUsesHttps();
const char* WebTransportName();
}  // namespace ug::embedded
