// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
namespace ug::embedded
{
// Boot-only configuration; changing Wi-Fi/radio settings requires an explicit reboot.
esp_err_t StartWifiAndClock();
enum class NetworkLink
{
  kEthernet = 0,
  kStation = 1,
  kAccessPoint = 2
};
void SetNetworkLink(NetworkLink, bool available);
}  // namespace ug::embedded
