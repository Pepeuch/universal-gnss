// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_telemetry_operation.hpp"
#include "ug_json.hpp"
#include "universal_gnss_driver/ublox_config_profile_builder.hpp"
#include "universal_gnss_driver/unicore_config_profile_builder.hpp"
#include <cmath>
#include <cstring>
namespace ug::embedded
{
bool DecodeDetectionOperation(std::string_view body, std::string& error)
{
  auto root = ParseBoundedObject(body, error);
  if (!root)
    return false;
  error = "explicit DETECT_RECEIVER confirmation required; no raw commands accepted";
  if (!OnlyKeys(root.get(), {"confirm"}))
    return false;
  auto* confirm = cJSON_GetObjectItemCaseSensitive(root.get(), "confirm");
  if (!cJSON_IsString(confirm) || std::strcmp(confirm->valuestring, "DETECT_RECEIVER") != 0)
    return false;
  error.clear();
  return true;
}
ReceiverPlanCapabilities InspectReceiverPlans(std::string_view model, bool verified)
{
  using namespace universal_gnss_driver;
  ReceiverPlanCapabilities out;
  out.telemetry = BuildTelemetryPlan({}, model, verified).ok();
  if (!verified)
    return out;
  BaseConfigRequest request;
  request.vendor = model == "ZED-F9P" ? ReceiverVendor::kUblox : ReceiverVendor::kUnicore;
  request.model = model;
  request.model_verified = verified;
  BaseDisableRequest disable;
  disable.vendor = request.vendor;
  disable.model = model;
  disable.model_verified = verified;
  out.disable_base = BuildBaseDisableProfile(disable).ok();
  request.confirm_persistent = true;
  request.mode = BaseMode::kFixed;
  out.fixed = BuildBaseConfigProfile(request).ok();
  request.mode = BaseMode::kSurveyIn;
  out.survey_in = BuildBaseConfigProfile(request).ok();
  request.mode = BaseMode::kUnicoreTimeOptimization;
  out.time_optimization = BuildBaseConfigProfile(request).ok();
  return out;
}
bool DecodeTelemetryOperation(std::string_view body, TelemetryOperation& out, std::string& error)
{
  auto root = ParseBoundedObject(body, error);
  if (!root)
    return false;
  error = "invalid telemetry operation or missing explicit confirmation";
  if (!OnlyKeys(root.get(),
                {"rate_hz",
                 "receiver_port",
                 "constellations",
                 "signal_groups",
                 "persist",
                 "confirm",
                 "persistent_confirmation"}))
    return false;
  auto item = [&](const char* name) { return cJSON_GetObjectItemCaseSensitive(root.get(), name); };
  auto text = [&](const char* name, const char* value) {
    auto* n = item(name);
    return cJSON_IsString(n) && std::strcmp(n->valuestring, value) == 0;
  };
  if (!text("confirm", "APPLY_GNSS_TELEMETRY"))
    return false;
  TelemetryOperation op;
  auto integer = [&](const char* name, std::uint8_t& value, unsigned max) {
    auto* n = item(name);
    if (!n)
      return true;
    if (!cJSON_IsNumber(n) || !std::isfinite(n->valuedouble) || n->valuedouble < 1 ||
        n->valuedouble > max || std::floor(n->valuedouble) != n->valuedouble)
      return false;
    value = static_cast<std::uint8_t>(n->valuedouble);
    return true;
  };
  if (!integer("rate_hz", op.rate_hz, 10) || !integer("receiver_port", op.receiver_port, 2) ||
      (op.rate_hz != 1 && op.rate_hz != 2 && op.rate_hz != 5 && op.rate_hz != 10))
    return false;
  if (auto* n = item("persist"))
  {
    if (!cJSON_IsBool(n))
      return false;
    op.persist = cJSON_IsTrue(n);
  }
  op.confirm_persistent = text("persistent_confirmation", "SAVE_RECEIVER_CONFIGURATION");
  if ((op.persist && !op.confirm_persistent) ||
      (item("persistent_confirmation") && !op.confirm_persistent))
    return false;
  if (auto* n = item("constellations"))
  {
    if (!cJSON_IsObject(n) || !OnlyKeys(n, {"gps", "galileo", "beidou", "glonass"}))
      return false;
    constexpr std::array<const char*, 4> names{"gps", "galileo", "beidou", "glonass"};
    bool any = false;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
      auto* value = cJSON_GetObjectItemCaseSensitive(n, names[i]);
      if (!cJSON_IsBool(value))
        return false;
      op.constellations[i] = cJSON_IsTrue(value);
      any |= op.constellations[i];
    }
    if (!any)
      return false;
    op.set_constellations = true;
  }
  if (auto* n = item("signal_groups"))
  {
    if (!cJSON_IsArray(n) || cJSON_GetArraySize(n) < 1 || cJSON_GetArraySize(n) > 2)
      return false;
    for (int i = 0; i < cJSON_GetArraySize(n); ++i)
    {
      const auto* value = cJSON_GetArrayItem(n, i);
      if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) || value->valuedouble < 1 ||
          value->valuedouble > 255 || std::floor(value->valuedouble) != value->valuedouble)
        return false;
      op.signal_groups[i] = static_cast<std::uint8_t>(value->valuedouble);
    }
    op.signal_group_count = cJSON_GetArraySize(n);
    if (op.signal_group_count == 2 && op.signal_groups[0] == op.signal_groups[1])
      return false;
  }
  out = op;
  error.clear();
  return true;
}
universal_gnss_driver::BaseConfigPlan
BuildTelemetryPlan(const TelemetryOperation& op, std::string_view model, bool verified)
{
  using namespace universal_gnss_driver;
  BaseConfigPlan plan;
  auto fail = [&](const char* reason) {
    plan.commands.clear();
    plan.error = reason;
    return plan;
  };
  if (!verified || (op.rate_hz != 1 && op.rate_hz != 2 && op.rate_hz != 5 && op.rate_hz != 10) ||
      op.receiver_port < 1 || op.receiver_port > 2 || op.signal_group_count > 2 ||
      (op.persist && !op.confirm_persistent))
    return fail("unverified model or invalid telemetry parameters");
  // N4 R1.6 section 4.23: changed SIGNALGROUP values persist and reset the
  // module automatically. A telemetry/persist confirmation does not authorize
  // that reset or establish a trusted post-reset receiver incarnation.
  if (op.signal_group_count)
    return fail("native SIGNALGROUP configuration unavailable: automatic persistence/reset "
                "requires a qualified lifecycle");
  if (model == "ZED-F9P")
  {
    using namespace universal_gnss_protocols;
    using namespace ubx_cfg_keys;
    UbloxConfigProfile profile;
    profile.config_kind = ReceiverConfigProfileKind::kDiagnosticsOutput;
    profile.measurement_rate_hz = op.rate_hz;
    profile.target_layers =
        op.persist
            ? std::vector<UbxCfgLayer>{UbxCfgLayer::kRam, UbxCfgLayer::kBbr, UbxCfgLayer::kFlash}
            : std::vector<UbxCfgLayer>{UbxCfgLayer::kRam};
    profile.port.output_ports = {op.receiver_port == 1 ? UbloxInterfacePort::kUart1
                                                       : UbloxInterfacePort::kUart2};
    const bool first = op.receiver_port == 1;
    for (auto key : {first ? kMsgoutUbxNavPvtUart1 : kMsgoutUbxNavPvtUart2,
                     first ? kMsgoutUbxNavSatUart1 : kMsgoutUbxNavSatUart2,
                     first ? kMsgoutUbxNavStatusUart1 : kMsgoutUbxNavStatusUart2,
                     first ? kMsgoutUbxNavDopUart1 : kMsgoutUbxNavDopUart2,
                     first ? kMsgoutNmeaGgaUart1 : kMsgoutNmeaGgaUart2})
      profile.enabled_messages.push_back({key, 1});
    if (op.set_constellations)
    {
      bool any = false;
      for (std::size_t i = 0; i < 4; ++i)
      {
        profile.constellations.push_back(
            {static_cast<UbxCfgConstellation>(i), op.constellations[i]});
        any |= op.constellations[i];
      }
      if (!any)
        return fail("at least one constellation required");
    }
    auto built = UbloxConfigProfileBuilder::Build(profile);
    if (built.status != UbloxConfigProfileBuildStatus::kOk)
      return fail(built.error_message.c_str());
    plan.commands = std::move(built.commands);
  }
  else if (model == "UM980" || model == "UM981" || model == "UM982")
  {
    if (op.set_constellations)
      return fail(
          "use model-qualified Unicore signal groups instead of u-blox constellation switches");
    // The existing Unicore builder has no port selector for telemetry logs.
    // These commands address the current command port. Do not claim an
    // explicit COM1/COM2 selector that this existing builder cannot express.
    if (op.receiver_port != 1)
      return fail("explicit Unicore telemetry output-port selection is unavailable");
    const auto& model_profile = ResolveUnicoreModelProfile(model);
    UnicoreConfigProfile profile;
    profile.target = BuildUnicoreTargetSelector(model_profile);
    profile.config_kind = ReceiverConfigProfileKind::kDiagnosticsOutput;
    // Deliberately not BuildUnicoreDiagnosticsProfile: that convenience preset
    // derives from a ROVER profile and would silently leave a configured BASE.
    profile.output_messages = {{UnicoreOutputMessageKind::kGpgga, 1.0 / op.rate_hz},
                               {UnicoreOutputMessageKind::kGpgsv, 1.0},
                               {UnicoreOutputMessageKind::kGpgst, 1.0 / op.rate_hz},
                               {UnicoreOutputMessageKind::kPvtslna, 1.0 / op.rate_hz},
                               {UnicoreOutputMessageKind::kBestnava, 1.0 / op.rate_hz},
                               {UnicoreOutputMessageKind::kRtkstatusa, 1.0},
                               {UnicoreOutputMessageKind::kRtcmstatusa, std::nullopt},
                               {UnicoreOutputMessageKind::kSatsinfoa, 1.0}};
    profile.persistence =
        op.persist ? UnicorePersistenceTarget::kSaveConfig : UnicorePersistenceTarget::kRuntimeOnly;
    auto built = UnicoreConfigProfileBuilder::Build(profile);
    if (built.status != UnicoreConfigProfileBuildStatus::kOk)
      return fail(built.error_message.c_str());
    plan.commands = std::move(built.commands);
  }
  else
    return fail("telemetry profile requires a supported verified model");
  if (plan.commands.empty() || plan.commands.size() > 32)
    return fail("telemetry command budget exceeded");
  for (auto& command : plan.commands)
  {
    command.explicit_safety_confirmation = op.confirm_persistent;
    command.failure_policy = ReceiverCommandFailurePolicy::kAbortOnFailure;
  }
  return plan;
}
}  // namespace ug::embedded
