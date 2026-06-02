#pragma once

#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_discovery.hpp"

namespace universal_gnss_tools
{

std::string FormatReceiverDiscoveryText(
    const std::vector<universal_gnss_driver::ReceiverProbeResult>& results);

std::string FormatReceiverDiscoveryJson(
    const std::vector<universal_gnss_driver::ReceiverProbeResult>& results);

}  // namespace universal_gnss_tools
