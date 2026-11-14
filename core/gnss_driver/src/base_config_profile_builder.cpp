// SPDX-License-Identifier: LGPL-3.0-only
#include "universal_gnss_driver/base_config_profile_builder.hpp"
#include "universal_gnss_driver/unicore_model_profile.hpp"
#include "universal_gnss_protocols/ubx_cfg_builder.hpp"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace universal_gnss_driver
{
namespace
{
std::string Number(const double value, const int precision)
{
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}
ReceiverCommand MakeCommand(const BaseConfigRequest& r, const bool persistent)
{
  ReceiverCommand c;
  c.kind = ReceiverCommandKind::kApplyConfigProfile;
  c.target =
      r.vendor == ReceiverVendor::kUnicore
          ? BuildUnicoreTargetSelector(ResolveUnicoreModelProfile(r.model))
          : ReceiverTargetSelector{ReceiverVendor::kUblox, "F9", "ZED-F9P", "ublox_f9p_base"};
  c.expected_response = r.vendor == ReceiverVendor::kUnicore ? ReceiverResponseKind::kTextPayload
                                                             : ReceiverResponseKind::kAck;
  c.retry_policy.timeout_ms = 2000;
  c.safety_level =
      persistent ? ReceiverCommandSafetyLevel::kPersistent : ReceiverCommandSafetyLevel::kRuntime;
  c.explicit_safety_confirmation = r.confirm_persistent;
  return c;
}
void AppendUbloxRtcmOutputs(std::vector<universal_gnss_protocols::UbxCfgKeyValue>& values,
                            std::uint8_t port,
                            bool enabled)
{
  using namespace universal_gnss_protocols;
  values.push_back({port == 1 ? 0x10740004u : 0x10760004u, UbxCfgValue::Boolean(enabled)});
  for (const auto key :
       {0x209102beu, 0x2091035fu, 0x20910364u, 0x20910369u, 0x2091036eu, 0x20910304u})
    values.push_back(
        {key + static_cast<std::uint32_t>(port - 1), UbxCfgValue::U1(enabled ? 1 : 0)});
}
}  // namespace
BaseConfigPlan BuildBaseConfigProfile(const BaseConfigRequest& r)
{
  BaseConfigPlan p;
  auto fail = [&](const char* reason) {
    p.commands.clear();
    p.error = reason;
    return p;
  };
  if ((r.mode != BaseMode::kFixed && r.mode != BaseMode::kSurveyIn &&
       r.mode != BaseMode::kUnicoreTimeOptimization) ||
      (r.height_datum != BaseHeightDatum::kEllipsoidal &&
       r.height_datum != BaseHeightDatum::kOrthometric))
    return fail("invalid BASE mode or height datum");
  if (!r.model_verified)
    return fail("verified model response required before BASE configuration");
  if (r.uart_port != 1 && r.uart_port != 2)
    return fail("unsupported receiver UART");
  if (!std::isfinite(r.latitude_deg) || !std::isfinite(r.longitude_deg) ||
      !std::isfinite(r.height_m) || std::abs(r.latitude_deg) > 90 ||
      std::abs(r.longitude_deg) > 180 || std::abs(r.height_m) > 30000)
    return fail("invalid geodetic coordinates");
  if (r.vendor == ReceiverVendor::kUnicore)
  {
    const auto model = ParseUnicoreModel(r.model);
    if (!model || (*model != UnicoreModel::kUm980 && *model != UnicoreModel::kUm982 &&
                   *model != UnicoreModel::kUm981))
      return fail("BASE command contract not qualified for this Unicore model");
    if (r.mode == BaseMode::kSurveyIn)
      return fail("Unicore TIME self-optimization is not F9P accuracy-limited Survey-In");
    // N4 R1.6 lists UM980/UM982 for TIME and UNDULATION, not UM981. Do not guess.
    if (*model == UnicoreModel::kUm981)
      return fail("UM981 BASE requires its model-specific datum/TIME command contract");
    auto text = [&](const std::string& line, const bool persistent = false) {
      auto c = MakeCommand(r, persistent);
      SetTextPayload(c, line + "\r\n");
      p.commands.push_back(std::move(c));
    };
    text(r.height_datum == BaseHeightDatum::kEllipsoidal ? "CONFIG UNDULATION 0.0"
                                                         : "CONFIG UNDULATION AUTO");
    if (r.mode == BaseMode::kFixed)
      text("MODE BASE " + Number(r.latitude_deg, 12) + " " + Number(r.longitude_deg, 12) + " " +
           Number(r.height_m, 4));
    else
    {
      if (r.duration_s == 0 || r.duration_s > 3600 || !std::isfinite(r.reuse_distance_m) ||
          r.reuse_distance_m < 0 || r.reuse_distance_m > 10)
        return fail("N4 TIME requires 1..3600 seconds and 0..10m reuse distance");
      // TIME mode writes its optimized position to Flash independently of SAVECONFIG.
      text("MODE BASE TIME " + std::to_string(r.duration_s) + " " + Number(r.reuse_distance_m, 4),
           true);
    }
    const auto port = " COM" + std::to_string(r.uart_port);
    for (const unsigned message : {1006u, 1074u, 1084u, 1094u, 1124u, 1230u})
      text(r.rtcm_output ? "RTCM" + std::to_string(message) + port + " 1"
                         : "UNLOG" + port + " RTCM" + std::to_string(message));
    if (r.persist)
      text("SAVECONFIG", true);
    return p;
  }
  if (r.vendor != ReceiverVendor::kUblox || (r.model != "ZED-F9P" && r.model != "F9P"))
    return fail("BASE configuration requires a supported verified receiver model");
  if (r.mode == BaseMode::kUnicoreTimeOptimization ||
      r.height_datum != BaseHeightDatum::kEllipsoidal)
    return fail("F9P requires ellipsoidal LLH and its own Survey-In contract");
  using namespace universal_gnss_protocols;
  std::vector<UbxCfgKeyValue> values;
  values.push_back({0x20030001u, UbxCfgValue::U1(r.mode == BaseMode::kFixed ? 2 : 1)});
  if (r.mode == BaseMode::kFixed)
  {
    values.push_back({0x20030002u, UbxCfgValue::U1(1)});
    const auto lat = std::llround(r.latitude_deg * 1e9), lon = std::llround(r.longitude_deg * 1e9),
               height = std::llround(r.height_m * 1e4);
    values.push_back({0x40030009u, UbxCfgValue::I4(static_cast<std::int32_t>(lat / 100))});
    values.push_back({0x4003000au, UbxCfgValue::I4(static_cast<std::int32_t>(lon / 100))});
    values.push_back({0x4003000bu, UbxCfgValue::I4(static_cast<std::int32_t>(height / 100))});
    values.push_back({0x2003000cu, UbxCfgValue::I1(static_cast<std::int8_t>(lat % 100))});
    values.push_back({0x2003000du, UbxCfgValue::I1(static_cast<std::int8_t>(lon % 100))});
    values.push_back({0x2003000eu, UbxCfgValue::I1(static_cast<std::int8_t>(height % 100))});
    values.push_back({0x4003000fu, UbxCfgValue::U4(r.accuracy_01mm)});
  }
  else
  {
    if (r.duration_s == 0 || r.accuracy_01mm == 0)
      return fail("invalid F9P survey limits");
    values.push_back({0x40030010u, UbxCfgValue::U4(r.duration_s)});
    values.push_back({0x40030011u, UbxCfgValue::U4(r.accuracy_01mm)});
  }
  // UART1/2 key families are separate and receiver-specific, never an ASCII command.
  AppendUbloxRtcmOutputs(values, r.uart_port, r.rtcm_output);
  std::vector<UbxCfgLayer> layers{UbxCfgLayer::kRam};
  if (r.persist)
  {
    layers.push_back(UbxCfgLayer::kBbr);
    layers.push_back(UbxCfgLayer::kFlash);
  }
  const auto built = BuildUbxCfgValsetFrame(layers, values);
  if (built.status != UbxCfgBuilderStatus::kOk)
    return fail("UG UBX configuration builder rejected BASE keys");
  auto c = MakeCommand(r, r.persist);
  SetBinaryPayload(c, built.frame);
  p.commands.push_back(std::move(c));
  return p;
}
BaseConfigPlan BuildBaseDisableProfile(const BaseDisableRequest& request)
{
  BaseConfigPlan plan;
  if (!request.model_verified || (request.uart_port != 1 && request.uart_port != 2))
  {
    plan.error = "BASE disable requires a verified supported model and receiver UART1/2";
    return plan;
  }
  if (request.vendor == ReceiverVendor::kUnicore &&
      (request.model == "UM980" || request.model == "UM982"))
  {
    BaseConfigRequest target;
    target.vendor = request.vendor;
    target.model = request.model;
    target.confirm_persistent = request.confirm_persistent;
    auto text = [&](const std::string& line, bool persistent = false) {
      auto command = MakeCommand(target, persistent);
      SetTextPayload(command, line + "\r\n");
      plan.commands.push_back(std::move(command));
    };
    // N4 R1.6 section 3: generic MODE ROVER exits BASE and starts RTK
    // initialization. Do not select the firmware-scoped scenarios of section 3.6.
    text("MODE ROVER");
    const auto port = " COM" + std::to_string(request.uart_port);
    for (const unsigned message : {1006u, 1074u, 1084u, 1094u, 1124u, 1230u})
      text("UNLOG" + port + " RTCM" + std::to_string(message));
    if (request.persist)
      text("SAVECONFIG", true);
    return plan;
  }
  if (request.vendor != ReceiverVendor::kUblox ||
      (request.model != "ZED-F9P" && request.model != "F9P"))
  {
    plan.error = "BASE disable model contract is not supported";
    return plan;
  }
  using namespace universal_gnss_protocols;
  // UBX-22008968 R01, section 6.9.28/Table 52: DISABLED = 0.
  std::vector<UbxCfgKeyValue> values{{0x20030001u, UbxCfgValue::U1(0)}};
  AppendUbloxRtcmOutputs(values, request.uart_port, false);
  std::vector<UbxCfgLayer> layers{UbxCfgLayer::kRam};
  if (request.persist)
  {
    layers.push_back(UbxCfgLayer::kBbr);
    layers.push_back(UbxCfgLayer::kFlash);
  }
  const auto built = BuildUbxCfgValsetFrame(layers, values);
  if (built.status != UbxCfgBuilderStatus::kOk)
  {
    plan.error = "UG UBX builder rejected BASE disable";
    return plan;
  }
  BaseConfigRequest target;
  target.vendor = request.vendor;
  target.model = request.model;
  target.confirm_persistent = request.confirm_persistent;
  auto command = MakeCommand(target, request.persist);
  SetBinaryPayload(command, built.frame);
  plan.commands.push_back(std::move(command));
  return plan;
}
}  // namespace universal_gnss_driver
