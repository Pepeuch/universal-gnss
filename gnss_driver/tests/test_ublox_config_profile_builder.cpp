#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command_dispatcher.hpp"
#include "universal_gnss_driver/ublox_config_profile_builder.hpp"
#include "universal_gnss_protocols/ubx_cfg_builder.hpp"
#include "universal_gnss_transport/memory_stream.hpp"

namespace
{

using universal_gnss_driver::DispatchStatus;
using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandDispatcher;
using universal_gnss_driver::ReceiverCommandKind;
using universal_gnss_driver::ReceiverCommandPayloadKind;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
using universal_gnss_driver::ReceiverConfigProfileKind;
using universal_gnss_driver::UbloxConfigProfile;
using universal_gnss_driver::UbloxConfigProfileBuildStatus;
using universal_gnss_driver::UbloxConfigProfileBuilder;
using universal_gnss_driver::UbloxConstellationConfig;
using universal_gnss_driver::UbloxMessageRate;
using universal_gnss_protocols::UbxCfgConstellation;
using universal_gnss_protocols::UbxCfgLayer;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutNmeaGgaUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxMonRfUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavPvtUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavSatUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavStatusUart1;
using universal_gnss_protocols::ubx_cfg_keys::kSignalGalEnable;
using universal_gnss_protocols::ubx_cfg_keys::kUart1Baudrate;
using universal_gnss_transport::MemoryByteSink;

struct TestContext
{
  int failures{0};

  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

std::vector<std::uint8_t> PackU32Le(const std::uint32_t value)
{
  return {
      static_cast<std::uint8_t>(value & 0xFFu),
      static_cast<std::uint8_t>((value >> 8u) & 0xFFu),
      static_cast<std::uint8_t>((value >> 16u) & 0xFFu),
      static_cast<std::uint8_t>((value >> 24u) & 0xFFu),
  };
}

bool ContainsBytes(const std::vector<std::uint8_t>& haystack,
                   const std::vector<std::uint8_t>& needle)
{
  if (needle.empty() || needle.size() > haystack.size())
  {
    return false;
  }

  return std::search(haystack.begin(),
                     haystack.end(),
                     needle.begin(),
                     needle.end()) != haystack.end();
}

void ExpectUbxValsetFrame(TestContext& ctx, const ReceiverCommand& command)
{
  ctx.Expect(command.payload.kind == ReceiverCommandPayloadKind::kBinary,
             "u-blox config profile commands should emit binary payloads");
  ctx.Expect(command.payload.binary.size() >= 8u &&
                 command.payload.binary[0] == 0xB5u &&
                 command.payload.binary[1] == 0x62u &&
                 command.payload.binary[2] == 0x06u &&
                 command.payload.binary[3] == 0x8Au,
             "u-blox config profile commands should wrap CFG-VALSET in a UBX frame");
}

void TestRoverProfileGeneratesExpectedCommands(TestContext& ctx)
{
  const auto profile = UbloxConfigProfileBuilder::BuildUbloxRoverProfile();
  const auto result = UbloxConfigProfileBuilder::Build(profile);

  ctx.Expect(result.status == UbloxConfigProfileBuildStatus::kOk,
             "u-blox rover profile should build successfully");
  ctx.Expect(profile.config_kind == ReceiverConfigProfileKind::kRover,
             "u-blox rover helper should declare the rover config profile kind");
  ctx.Expect(result.commands.size() == 9u,
             "u-blox rover helper should generate one rate command, four message commands, and four constellation commands");

  std::size_t protocol_output_commands = 0u;
  for (const auto& command : result.commands)
  {
    ExpectUbxValsetFrame(ctx, command);
    if (command.kind == ReceiverCommandKind::kSetProtocolOutputs)
    {
      ++protocol_output_commands;
    }
  }

  ctx.Expect(protocol_output_commands == 4u,
             "u-blox rover helper should tag message-rate changes as protocol-output commands");
}

void TestBaudrateAndMeasurementRateGeneration(TestContext& ctx)
{
  UbloxConfigProfile profile;
  profile.port.uart1_baudrate = 460800u;
  profile.measurement_rate_hz = 5.0;

  const auto result = UbloxConfigProfileBuilder::Build(profile);
  ctx.Expect(result.status == UbloxConfigProfileBuildStatus::kOk &&
                 result.commands.size() == 2u,
             "explicit baudrate and measurement rate should each generate a command");

  const auto baud_key = PackU32Le(kUart1Baudrate);
  const std::vector<std::uint8_t> baud_value = {0x00u, 0x08u, 0x07u, 0x00u};
  ctx.Expect(ContainsBytes(result.commands[0].payload.binary, baud_key) &&
                 ContainsBytes(result.commands[0].payload.binary, baud_value),
             "u-blox baudrate command should pack the UART1 baud CFG key and U4 little-endian value");
}

void TestMessageEnableDisableGeneration(TestContext& ctx)
{
  UbloxConfigProfile profile;
  profile.enabled_messages = {
      UbloxMessageRate{kMsgoutUbxNavPvtUart1, 1u},
      UbloxMessageRate{kMsgoutUbxMonRfUart1, 2u},
  };
  profile.disabled_messages = {kMsgoutNmeaGgaUart1};

  const auto result = UbloxConfigProfileBuilder::Build(profile);
  ctx.Expect(result.status == UbloxConfigProfileBuildStatus::kOk &&
                 result.commands.size() == 3u,
             "message enable/disable requests should map to one command per message key");

  const auto nav_pvt = PackU32Le(kMsgoutUbxNavPvtUart1);
  const auto mon_rf = PackU32Le(kMsgoutUbxMonRfUart1);
  const auto nmea_gga = PackU32Le(kMsgoutNmeaGgaUart1);
  ctx.Expect(ContainsBytes(result.commands[0].payload.binary, nav_pvt) &&
                 ContainsBytes(result.commands[0].payload.binary, {0x01u}),
             "enabled NAV-PVT message should carry its CFG key and rate value");
  ctx.Expect(ContainsBytes(result.commands[1].payload.binary, mon_rf) &&
                 ContainsBytes(result.commands[1].payload.binary, {0x02u}),
             "enabled MON-RF message should carry its CFG key and configured rate");
  ctx.Expect(ContainsBytes(result.commands[2].payload.binary, nmea_gga) &&
                 ContainsBytes(result.commands[2].payload.binary, {0x00u}),
             "disabled message command should carry the CFG key and a zero rate");
}

void TestConstellationAndSafetyPolicy(TestContext& ctx)
{
  UbloxConfigProfile profile;
  profile.target_layers = {UbxCfgLayer::kRam, UbxCfgLayer::kFlash};
  profile.constellations = {
      UbloxConstellationConfig{UbxCfgConstellation::kGalileo, false},
  };

  const auto result = UbloxConfigProfileBuilder::Build(profile);
  ctx.Expect(result.status == UbloxConfigProfileBuildStatus::kOk &&
                 result.commands.size() == 1u,
             "constellation toggles should generate one command per constellation");
  ctx.Expect(result.commands[0].safety_level == ReceiverCommandSafetyLevel::kPersistent,
             "u-blox commands targeting persistent CFG layers should be marked persistent");
  ctx.Expect(ContainsBytes(result.commands[0].payload.binary, PackU32Le(kSignalGalEnable)) &&
                 ContainsBytes(result.commands[0].payload.binary, {0x00u}),
             "disabled constellation command should carry the documented GAL enable key and false value");
}

void TestDispatchSafetyIntegration(TestContext& ctx)
{
  {
    const auto runtime_profile = UbloxConfigProfileBuilder::BuildUbloxRoverProfile();
    const auto result = UbloxConfigProfileBuilder::Build(runtime_profile);
    MemoryByteSink sink;
    ReceiverCommandDispatcher dispatcher(sink);

    const auto dispatch = dispatcher.Dispatch(result.commands.front());
    ctx.Expect(dispatch.status == DispatchStatus::kSent &&
                   dispatcher.metrics().commands_sent == 1u,
               "runtime-only u-blox config commands should be dispatchable without extra confirmation");
  }

  {
    auto persistent_profile = UbloxConfigProfileBuilder::BuildUbloxDiagnosticsProfile(
        ReceiverCommandSafetyLevel::kRuntime, {UbxCfgLayer::kRam, UbxCfgLayer::kBbr});
    const auto result = UbloxConfigProfileBuilder::Build(persistent_profile);
    MemoryByteSink sink;
    ReceiverCommandDispatcher dispatcher(sink);

    const auto rejected = dispatcher.Dispatch(result.commands.front());
    ctx.Expect(rejected.status == DispatchStatus::kRejectedSafety,
               "persistent-layer u-blox config commands should be rejected until explicitly confirmed");

    ReceiverCommand confirmed = result.commands.front();
    confirmed.explicit_safety_confirmation = true;
    const auto accepted = dispatcher.Dispatch(confirmed);
    ctx.Expect(accepted.status == DispatchStatus::kSent,
               "confirmed persistent u-blox config commands should become dispatchable");
  }
}

void TestInvalidProfileInputs(TestContext& ctx)
{
  UbloxConfigProfile empty_layers;
  empty_layers.target_layers.clear();
  auto result = UbloxConfigProfileBuilder::Build(empty_layers);
  ctx.Expect(result.status == UbloxConfigProfileBuildStatus::kInvalidArgument,
             "profiles without CFG layers should be rejected");

  UbloxConfigProfile invalid_rate;
  invalid_rate.measurement_rate_hz = 0.0;
  result = UbloxConfigProfileBuilder::Build(invalid_rate);
  ctx.Expect(result.status == UbloxConfigProfileBuildStatus::kInvalidArgument,
             "profiles with non-positive measurement rates should be rejected");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestRoverProfileGeneratesExpectedCommands(ctx);
  TestBaudrateAndMeasurementRateGeneration(ctx);
  TestMessageEnableDisableGeneration(ctx);
  TestConstellationAndSafetyPolicy(ctx);
  TestDispatchSafetyIntegration(ctx);
  TestInvalidProfileInputs(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver u-blox config profile builder tests passed\n";
  return EXIT_SUCCESS;
}
