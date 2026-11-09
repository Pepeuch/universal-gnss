#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "universal_gnss_ntrip/ntrip_config.hpp"

namespace universal_gnss_ntrip
{

struct NtripRequest
{
  std::string mountpoint_path{};
  std::string request_text{};
  bool includes_authorization{false};
  bool includes_ntrip_version_header{false};
};

std::string NormalizeMountpointPath(std::string_view mountpoint);

std::string BuildBasicAuthorizationValue(std::string_view username, std::string_view password);

std::string BuildAuthorizationHeader(std::string_view username, std::string_view password);

NtripRequest BuildNtripGetRequest(const NtripConfig& config);

}  // namespace universal_gnss_ntrip
