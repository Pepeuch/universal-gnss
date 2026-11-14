// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_base_operation.hpp"
#include "ug_json.hpp"
#include <cmath>
#include <cstring>
namespace ug::embedded
{
bool DecodeBaseDisableOperation(std::string_view body,
                                BaseDisableOperation& out,
                                std::string& error)
{
  const auto root = ParseBoundedObject(body, error);
  if (!root)
    return false;
  error = "invalid BASE disable request or missing explicit confirmation";
  if (!OnlyKeys(root.get(), {"confirm", "receiver_port", "persist", "persistent_confirmation"}))
    return false;
  const auto text = [&](const char* key, const char* expected) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(root.get(), key);
    return cJSON_IsString(value) && std::strcmp(value->valuestring, expected) == 0;
  };
  if (!text("confirm", "DISABLE_BASE_MODE"))
    return false;
  BaseDisableOperation operation;
  if (const auto* port = cJSON_GetObjectItemCaseSensitive(root.get(), "receiver_port"))
  {
    if (!cJSON_IsNumber(port) || (port->valuedouble != 1 && port->valuedouble != 2))
      return false;
    operation.receiver_port = static_cast<std::uint8_t>(port->valuedouble);
  }
  if (const auto* persist = cJSON_GetObjectItemCaseSensitive(root.get(), "persist"))
  {
    if (!cJSON_IsBool(persist))
      return false;
    operation.persist = cJSON_IsTrue(persist);
  }
  operation.confirm_persistent = text("persistent_confirmation", "SAVE_RECEIVER_CONFIGURATION");
  if ((operation.persist && !operation.confirm_persistent) ||
      (cJSON_GetObjectItemCaseSensitive(root.get(), "persistent_confirmation") &&
       !operation.confirm_persistent))
    return false;
  out = operation;
  error.clear();
  return true;
}
bool DecodeBaseOperation(std::string_view body, BaseOperation& out, std::string& error)
{
  auto root = ParseBoundedObject(body, error);
  if (!root)
    return false;
  error = "invalid BASE operation or missing explicit confirmation";
  if (!OnlyKeys(root.get(),
                {"mode",
                 "height_datum",
                 "latitude_deg",
                 "longitude_deg",
                 "height_m",
                 "duration_s",
                 "accuracy_01mm",
                 "reuse_distance_m",
                 "rtcm_output",
                 "receiver_port",
                 "persist",
                 "confirm",
                 "persistent_confirmation"}))
    return false;
  auto item = [&](const char* name) { return cJSON_GetObjectItemCaseSensitive(root.get(), name); };
  auto text = [&](const char* name, const char* value) {
    auto* node = item(name);
    return cJSON_IsString(node) && std::strcmp(node->valuestring, value) == 0;
  };
  if (!text("confirm", "APPLY_BASE_CONFIGURATION"))
    return false;
  BaseOperation operation;
  if (text("mode", "fixed"))
    operation.mode = universal_gnss_driver::BaseMode::kFixed;
  else if (text("mode", "survey-in"))
    operation.mode = universal_gnss_driver::BaseMode::kSurveyIn;
  else if (text("mode", "time"))
    operation.mode = universal_gnss_driver::BaseMode::kUnicoreTimeOptimization;
  else
    return false;
  if (item("height_datum"))
  {
    if (text("height_datum", "ellipsoidal"))
      operation.datum = universal_gnss_driver::BaseHeightDatum::kEllipsoidal;
    else if (text("height_datum", "orthometric"))
      operation.datum = universal_gnss_driver::BaseHeightDatum::kOrthometric;
    else
      return false;
  }
  auto number = [&](const char* name,
                    double& value,
                    double min,
                    double max,
                    bool required = false,
                    bool integer = false) {
    auto* node = item(name);
    if (!node)
      return !required;
    if (!cJSON_IsNumber(node) || !std::isfinite(node->valuedouble) || node->valuedouble < min ||
        node->valuedouble > max || (integer && std::floor(node->valuedouble) != node->valuedouble))
      return false;
    value = node->valuedouble;
    return true;
  };
  const bool fixed = operation.mode == universal_gnss_driver::BaseMode::kFixed;
  if (!number("latitude_deg", operation.latitude_deg, -90, 90, fixed) ||
      !number("longitude_deg", operation.longitude_deg, -180, 180, fixed) ||
      !number("height_m", operation.height_m, -1000, 100000, fixed) ||
      !number("reuse_distance_m", operation.reuse_distance_m, 0, 10))
    return false;
  double port = operation.receiver_port;
  if (!number("receiver_port", port, 1, 2, false, true))
    return false;
  operation.receiver_port = static_cast<std::uint8_t>(port);
  double duration = operation.duration_s, accuracy = operation.accuracy_01mm;
  if (!number("duration_s", duration, 1, 86400, false, true) ||
      !number("accuracy_01mm", accuracy, 1, 100000000, false, true))
    return false;
  operation.duration_s = static_cast<std::uint32_t>(duration);
  operation.accuracy_01mm = static_cast<std::uint32_t>(accuracy);
  auto boolean = [&](const char* name, bool& value) {
    auto* n = item(name);
    if (!n)
      return true;
    if (!cJSON_IsBool(n))
      return false;
    value = cJSON_IsTrue(n);
    return true;
  };
  if (!boolean("persist", operation.persist) || !boolean("rtcm_output", operation.rtcm_output))
    return false;
  operation.confirm_persistent = text("persistent_confirmation", "SAVE_RECEIVER_CONFIGURATION");
  if ((operation.persist ||
       operation.mode == universal_gnss_driver::BaseMode::kUnicoreTimeOptimization) &&
      !operation.confirm_persistent)
    return false;
  if (item("persistent_confirmation") && !operation.confirm_persistent)
    return false;
  out = operation;
  error.clear();
  return true;
}
}  // namespace ug::embedded
