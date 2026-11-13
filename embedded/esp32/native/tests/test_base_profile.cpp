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
}
