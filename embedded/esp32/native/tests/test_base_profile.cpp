#include "ug_base_operation.hpp"
// SPDX-License-Identifier: LGPL-3.0-only
#include "universal_gnss_driver/base_config_profile_builder.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <map>
using namespace universal_gnss_driver;
using namespace universal_gnss_protocols;
static std::map<std::uint32_t, std::uint32_t> Keys(const BaseConfigPlan& plan)
{
  assert(plan.ok() && plan.commands.size() == 1);
  UbxFrameFramer f;
  std::optional<UbxFrame> frame;
  for (const auto b : plan.commands.front().payload.binary)
  {
    const auto r = f.PushByte(b);
    if (r.record)
      frame = r.record;
  }
  assert(frame && frame->checksum_status == ChecksumStatus::kValid);
  assert(frame->class_id == 6 && frame->message_id == 0x8a);
  auto data = frame->payload;
  std::map<std::uint32_t, std::uint32_t> result;
  for (std::size_t pos = 4; pos < data.size();)
  {
    assert(data.size() - pos >= 5);
    std::uint32_t k = 0, v = 0;
    for (unsigned i = 0; i < 4; ++i)
      k |= static_cast<std::uint32_t>(data[pos++]) << (8 * i);
    const auto width = (k >> 28) == 4 ? 4u : 1u;
    assert(data.size() - pos >= width);
    for (unsigned i = 0; i < width; ++i)
      v |= static_cast<std::uint32_t>(data[pos++]) << (8 * i);
    result[k] = v;
  }
  return result;
}
int main()
{
  BaseDisableRequest disable;
  assert(!BuildBaseDisableProfile(disable).ok());
  disable.vendor = ReceiverVendor::kUblox;
  disable.model = "ZED-F9P";
  assert(!BuildBaseDisableProfile(disable).ok());
  disable.model_verified = true;
  for (std::uint8_t port : {1, 2})
  {
    disable.uart_port = port;
    const auto plan = BuildBaseDisableProfile(disable);
    const auto stopped = Keys(plan);
    assert(stopped.size() == 8 && stopped.at(0x20030001u) == 0);
    assert(stopped.at(port == 1 ? 0x10740004u : 0x10760004u) == 0);
    assert(stopped.count(port == 1 ? 0x10760004u : 0x10740004u) == 0);
    assert(stopped.count(0x40030009u) == 0 &&
           stopped.count(0x40030010u) == 0);  // No coordinate/duration erase.
    for (const auto& [key, value] : stopped)
      assert(value == 0);
    assert(plan.commands[0].payload.binary[7] == 1 && HasSafeDispatchApproval(plan.commands[0]));
  }
  disable.persist = true;
  auto disabled_plan = BuildBaseDisableProfile(disable);
  assert(disabled_plan.ok() && !HasSafeDispatchApproval(disabled_plan.commands[0]));
  disable.confirm_persistent = true;
  disabled_plan = BuildBaseDisableProfile(disable);
  assert(disabled_plan.commands[0].payload.binary[7] == 7 &&
         HasSafeDispatchApproval(disabled_plan.commands[0]));
  disable.uart_port = 0;
  assert(!BuildBaseDisableProfile(disable).ok());
  disable.uart_port = 1;
  disable.vendor = ReceiverVendor::kUnicore;
  disable.model = "UM982";
  assert(!BuildBaseDisableProfile(disable).ok());
  disable.vendor = ReceiverVendor::kUblox;
  disable.model = "F10";
  assert(!BuildBaseDisableProfile(disable).ok());
  ug::embedded::BaseDisableOperation disable_operation;
  std::string disable_error;
  assert(ug::embedded::DecodeBaseDisableOperation(
      R"({"confirm":"DISABLE_BASE_MODE"})", disable_operation, disable_error));
  assert(!disable_operation.persist && disable_operation.receiver_port == 1);
  assert(ug::embedded::DecodeBaseDisableOperation(
      R"({"confirm":"DISABLE_BASE_MODE","receiver_port":2,"persist":true,"persistent_confirmation":"SAVE_RECEIVER_CONFIGURATION"})",
      disable_operation,
      disable_error));
  assert(disable_operation.persist && disable_operation.receiver_port == 2 &&
         disable_operation.confirm_persistent);
  for (auto bad : {"{}",
                   R"({"confirm":"SURVEY STOP"})",
                   R"({"confirm":"DISABLE_BASE_MODE","persist":true})",
                   R"({"confirm":"DISABLE_BASE_MODE","receiver_port":1.5})",
                   R"({"confirm":"DISABLE_BASE_MODE","persist":1})",
                   R"({"confirm":"DISABLE_BASE_MODE","factory_reset":true})",
                   R"({"confirm":"DISABLE_BASE_MODE","confirm":"DISABLE_BASE_MODE"})"})
  {
    assert(!ug::embedded::DecodeBaseDisableOperation(bad, disable_operation, disable_error));
    assert(disable_operation.persist &&
           disable_operation.receiver_port == 2);  // Failed parse does not publish partial output.
  }
  BaseConfigRequest r;
  assert(!BuildBaseConfigProfile(r).ok());
  r.model_verified = true;
  r.vendor = ReceiverVendor::kUblox;
  r.model = "ZED-F9P";
  r.latitude_deg = -12.345678912;
  r.longitude_deg = 179.999999999;
  r.height_m = -3.4321;
  auto p = BuildBaseConfigProfile(r);
  auto keys = Keys(p);
  assert(keys[0x20030001] == 2 && keys[0x20030002] == 1);
  assert(static_cast<std::int32_t>(keys[0x40030009]) == -123456789);
  assert(static_cast<std::int8_t>(keys[0x2003000c]) == -12);
  assert(keys[0x4003000a] == 1799999999 && keys[0x2003000d] == 99);
  assert(static_cast<std::int32_t>(keys[0x4003000b]) == -343);
  assert(static_cast<std::int8_t>(keys[0x2003000e]) == -21);
  assert(keys[0x209102be] == 1 && keys[0x10740004] == 1);
  r.persist = true;
  p = BuildBaseConfigProfile(r);
  assert(p.ok() && !HasSafeDispatchApproval(p.commands[0]));
  r.confirm_persistent = true;
  p = BuildBaseConfigProfile(r);
  assert(HasSafeDispatchApproval(p.commands[0]));
  r.mode = BaseMode::kSurveyIn;
  r.duration_s = 600;
  r.accuracy_01mm = 12345;
  keys = Keys(BuildBaseConfigProfile(r));
  assert(keys[0x20030001] == 1 && keys[0x40030010] == 600 && keys[0x40030011] == 12345);
  r.duration_s = 0;
  assert(!BuildBaseConfigProfile(r).ok());
  r.duration_s = 600;
  r.model = "F10";
  assert(!BuildBaseConfigProfile(r).ok());
  r.vendor = ReceiverVendor::kUnicore;
  r.model = "UM982";
  r.persist = false;
  r.confirm_persistent = false;
  assert(!BuildBaseConfigProfile(r).ok());  // F9P survey semantics cannot leak to N4.
  r.mode = BaseMode::kUnicoreTimeOptimization;
  p = BuildBaseConfigProfile(r);
  assert(p.ok());
  assert(p.commands[1].payload.text == "MODE BASE TIME 600 0.0000\r\n");
  assert(!HasSafeDispatchApproval(p.commands[1]));  // TIME saves coordinates in Flash itself.
  r.confirm_persistent = true;
  p = BuildBaseConfigProfile(r);
  assert(HasSafeDispatchApproval(p.commands[1]));
  r.reuse_distance_m = 10.0001;
  assert(!BuildBaseConfigProfile(r).ok());
  r.reuse_distance_m = 0;
  r.mode = BaseMode::kFixed;
  r.rtcm_output = false;
  p = BuildBaseConfigProfile(r);
  assert(p.ok());
  assert(p.commands[0].payload.text == "CONFIG UNDULATION 0.0\r\n");
  assert(p.commands[1].payload.text.find("-12.345678912000") != std::string::npos);
  assert(p.commands[2].payload.text == "UNLOG COM1 RTCM1006\r\n");
  r.model = "UM981";
  assert(!BuildBaseConfigProfile(r).ok());
  r.model = "UM980";
  assert(BuildBaseConfigProfile(r).ok());
  r.latitude_deg = std::numeric_limits<double>::quiet_NaN();
  assert(!BuildBaseConfigProfile(r).ok());
  r.latitude_deg = 90.0001;
  assert(!BuildBaseConfigProfile(r).ok());
  r.latitude_deg = 90;
  r.uart_port = 0;
  assert(!BuildBaseConfigProfile(r).ok());
  r.uart_port = 1;
  r.mode = static_cast<BaseMode>(255);
  assert(!BuildBaseConfigProfile(r).ok());
  ug::embedded::BaseOperation operation;
  std::string parse_error;
  assert(ug::embedded::DecodeBaseOperation(
      R"({"mode":"fixed","latitude_deg":48.123456789,"longitude_deg":2.123456789,"height_m":123.4567,"confirm":"APPLY_BASE_CONFIGURATION"})",
      operation,
      parse_error));
  assert(operation.latitude_deg == 48.123456789);
  for (const auto* invalid :
       {R"({"mode":"fixed","confirm":"APPLY_BASE_CONFIGURATION"})",
        R"({"mode":"time","confirm":"APPLY_BASE_CONFIGURATION"})",
        R"({"mode":"survey-in","duration_s":1.5,"confirm":"APPLY_BASE_CONFIGURATION"})",
        R"({"mode":"fixed","model_verified":true})"})
    assert(!ug::embedded::DecodeBaseOperation(invalid, operation, parse_error));
  assert(ug::embedded::DecodeBaseOperation(
      R"({"mode":"time","confirm":"APPLY_BASE_CONFIGURATION","persistent_confirmation":"SAVE_RECEIVER_CONFIGURATION"})",
      operation,
      parse_error));
  assert(operation.confirm_persistent);
  assert(ug::embedded::DecodeBaseOperation(
      R"({"mode":"survey-in","receiver_port":2,"confirm":"APPLY_BASE_CONFIGURATION"})",
      operation,
      parse_error));
  assert(operation.receiver_port == 2);
  assert(!ug::embedded::DecodeBaseOperation(
      R"({"mode":"survey-in","receiver_port":3,"confirm":"APPLY_BASE_CONFIGURATION"})",
      operation,
      parse_error));
}
