// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_json.hpp"
#include <cstring>
namespace ug::embedded
{
inline bool DecodeNetworkRestart(std::string_view json, std::string& error)
{
  const auto root = ParseBoundedObject(json, error);
  if (!root)
    return false;
  const auto* confirm = cJSON_GetObjectItemCaseSensitive(root.get(), "confirm");
  if (!OnlyKeys(root.get(), {"confirm"}) || !cJSON_IsString(confirm) ||
      std::strcmp(confirm->valuestring, "RESTART_NTRIP_SERVICES") != 0)
  {
    error = "explicit RESTART_NTRIP_SERVICES confirmation required";
    return false;
  }
  error.clear();
  return true;
}
}  // namespace ug::embedded
