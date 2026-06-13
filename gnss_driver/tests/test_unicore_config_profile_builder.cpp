#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command_dispatcher.hpp"
#include "universal_gnss_driver/unicore_config_profile_builder.hpp"
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
using universal_gnss_driver::ReceiverVendor;
using universal_gnss_driver::UnicoreConfigProfile;
using universal_gnss_driver::UnicoreConfigProfileBuildStatus;
using universal_gnss_driver::UnicoreConfigProfileBuilder;
using universal_gnss_driver::UnicoreMode;
using universal_gnss_driver::UnicoreNmeaVersion;
using universal_gnss_driver::UnicoreOutputMessageKind;
using universal_gnss_driver::UnicoreOutputMessageRate;
using universal_gnss_driver::UnicorePersistenceTarget;
using universal_gnss_driver::UnicoreSignalConfig;
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

bool ContainsText(const ReceiverCommand& command, const std::string& text)
{
  return command.payload.kind == ReceiverCommandPayloadKind::kText &&
         command.payload.text.find(text) != std::string::npos;
}

void ExpectTextCommand(TestContext& ctx,
                       const ReceiverCommand& command,
                       const ReceiverCommandKind kind,
                       const ReceiverCommandSafetyLevel safety_level,
                       const std::string& text_prefix)
{
  ctx.Expect(command.kind == kind,
             "unicore command should preserve the expected command kind");
  ctx.Expect(command.safety_level == safety_level,
             "unicore command should preserve the expected safety level");
  ctx.Expect(command.target.vendor == ReceiverVendor::kUnicore &&
                 command.target.profile_id == "unicore_um98x_placeholder",
             "unicore command should target the built-in Unicore receiver profile");
  ctx.Expect(command.payload.kind == ReceiverCommandPayloadKind::kText &&
                 command.payload.text.rfind(text_prefix, 0u) == 0u &&
                 command.payload.text.size() >= 2u &&
                 command.payload.text.substr(command.payload.text.size() - 2u) == "\r\n",
             "unicore config builder should emit CRLF-terminated text commands");
}

void TestRoverProfileGeneration(TestContext& ctx)
{
  const auto profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile();
  const auto result = UnicoreConfigProfileBuilder::Build(profile);

  ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk,
             "unicore rover profile should build successfully");
  ctx.Expect(profile.config_kind == ReceiverConfigProfileKind::kRover,
             "unicore rover helper should declare the rover config profile kind");
  ctx.Expect(profile.clear_current_port_outputs,
             "unicore rover helper should request a runtime output cleanup before re-enabling logs");
  ctx.Expect(result.commands.size() == 13u,
             "unicore rover helper should generate mode, config, and output-message commands");

  ExpectTextCommand(ctx,
                    result.commands[0],
                    ReceiverCommandKind::kApplyConfigProfile,
                    ReceiverCommandSafetyLevel::kRuntime,
                    "MODE ROVER");
  ExpectTextCommand(ctx,
                    result.commands[1],
                    ReceiverCommandKind::kApplyConfigProfile,
                    ReceiverCommandSafetyLevel::kRuntime,
                    "CONFIG NMEA0183 V411");
  ctx.Expect(ContainsText(result.commands[4], "CONFIG DGPS TIMEOUT 600"),
             "unicore rover helper should include the conservative DGPS timeout command");
  ctx.Expect(ContainsText(result.commands[5], "UNLOG"),
             "unicore rover helper should clear the current port outputs before enabling the curated log set");
  ctx.Expect(ContainsText(result.commands[6], "LOG GPGGA ONTIME 0.2"),
             "unicore rover helper should enable GPGGA with the documented ONTIME syntax");
  ctx.Expect(ContainsText(result.commands[7], "GPGSV 1"),
             "unicore rover helper should enable GPGSV so portable visibility and CN0 fallback stay available");
  ctx.Expect(ContainsText(result.commands[8], "GPGST 1"),
             "unicore rover helper should enable GPGST so portable accuracy fallback stays available");
  ctx.Expect(ContainsText(result.commands[9], "LOG PVTSLNA ONTIME 0.2"),
             "unicore rover helper should enable PVTSLNA with the practical ONTIME syntax");
  ctx.Expect(ContainsText(result.commands[10], "BESTNAVA 0.2"),
             "unicore rover helper should emit BESTNAVA with direct-period syntax");
  ctx.Expect(ContainsText(result.commands[11], "RTKSTATUSA 1"),
             "unicore rover helper should emit RTKSTATUSA with direct-period syntax");
  ctx.Expect(ContainsText(result.commands[12], "RTCMSTATUSA ONCHANGED"),
             "unicore rover helper should emit RTCMSTATUSA with ONCHANGED syntax");
}

void TestDiagnosticsProfileGeneration(TestContext& ctx)
{
  const auto profile = UnicoreConfigProfileBuilder::BuildUnicoreDiagnosticsProfile();
  const auto result = UnicoreConfigProfileBuilder::Build(profile);

  ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk &&
                 result.commands.size() == 14u,
             "unicore diagnostics helper should extend the rover profile with one extra output command");
  ctx.Expect(ContainsText(result.commands.back(), "SATSINFOA 1"),
             "unicore diagnostics helper should enable SATSINFOA at a stable 1 Hz period");
}

void TestPersistentAndSignalGroupSafety(TestContext& ctx)
{
  {
    const auto profile =
        UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(
            UnicorePersistenceTarget::kSaveConfig);
    const auto result = UnicoreConfigProfileBuilder::Build(profile);

    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk &&
                   !result.commands.empty(),
               "persistent unicore rover profile should still generate commands");
    const auto& save_command = result.commands.back();
    ExpectTextCommand(ctx,
                      save_command,
                      ReceiverCommandKind::kApplyConfigProfile,
                      ReceiverCommandSafetyLevel::kPersistent,
                      "SAVECONFIG");

    MemoryByteSink sink;
    ReceiverCommandDispatcher dispatcher(sink);
    const auto rejected = dispatcher.Dispatch(save_command);
    ctx.Expect(rejected.status == DispatchStatus::kRejectedSafety,
               "persistent unicore save commands should require explicit confirmation");

    ReceiverCommand confirmed = save_command;
    confirmed.explicit_safety_confirmation = true;
    const auto accepted = dispatcher.Dispatch(confirmed);
    ctx.Expect(accepted.status == DispatchStatus::kSent,
               "confirmed persistent unicore save commands should become dispatchable");
  }

  {
    UnicoreConfigProfile profile;
    profile.signal_config = UnicoreSignalConfig{{3u, 6u}};
    const auto result = UnicoreConfigProfileBuilder::Build(profile);
    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk &&
                   result.commands.size() == 1u,
               "unicore signal-group config should generate a single command");
    ExpectTextCommand(ctx,
                      result.commands.front(),
                      ReceiverCommandKind::kApplyConfigProfile,
                      ReceiverCommandSafetyLevel::kPersistent,
                      "CONFIG SIGNALGROUP 3 6");
  }
}

void TestFactoryResetProfileGeneration(TestContext& ctx)
{
  const auto profile = UnicoreConfigProfileBuilder::BuildUnicoreFactoryResetProfile();
  const auto result = UnicoreConfigProfileBuilder::Build(profile);

  ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk &&
                 result.commands.size() == 1u,
             "unicore factory-reset helper should generate exactly one reset command");
  ExpectTextCommand(ctx,
                    result.commands.front(),
                    ReceiverCommandKind::kReset,
                    ReceiverCommandSafetyLevel::kFactoryReset,
                    "FRESET");

  MemoryByteSink sink;
  ReceiverCommandDispatcher dispatcher(sink);
  const auto rejected = dispatcher.Dispatch(result.commands.front());
  ctx.Expect(rejected.status == DispatchStatus::kRejectedSafety,
             "unicore factory-reset commands should require explicit safety confirmation");
}

void TestInvalidDeferredInputs(TestContext& ctx)
{
  {
    UnicoreConfigProfile profile;
    profile.mode = UnicoreMode::kBase;
    const auto result = UnicoreConfigProfileBuilder::Build(profile);
    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kInvalidArgument,
               "unicore base-mode orchestration should stay deferred from the portable builder");
  }

  {
    UnicoreConfigProfile profile;
    profile.nmea_version = UnicoreNmeaVersion::kV410;
    profile.output_messages = {
        {UnicoreOutputMessageKind::kBestnava, std::nullopt},
    };
    const auto result = UnicoreConfigProfileBuilder::Build(profile);
    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kInvalidArgument,
               "periodic unicore output messages should reject missing periods");
  }

  {
    UnicoreConfigProfile profile;
    profile.signal_config = UnicoreSignalConfig{};
    const auto result = UnicoreConfigProfileBuilder::Build(profile);
    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kInvalidArgument,
               "empty unicore signal-group settings should be rejected");
  }

  {
    UnicoreConfigProfile profile = UnicoreConfigProfileBuilder::BuildUnicoreFactoryResetProfile();
    profile.output_messages = {
        {UnicoreOutputMessageKind::kGpgga, 1.0},
    };
    const auto result = UnicoreConfigProfileBuilder::Build(profile);
    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kInvalidArgument,
               "factory-reset profiles should reject mixed portable config mutations");
  }
}

void TestRuntimeDispatcherBehavior(TestContext& ctx)
{
  const auto result = UnicoreConfigProfileBuilder::Build(
      UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile());
  MemoryByteSink sink;
  ReceiverCommandDispatcher dispatcher(sink);

  const auto dispatch = dispatcher.Dispatch(result.commands.front());
  ctx.Expect(dispatch.status == DispatchStatus::kSent &&
                 dispatcher.metrics().commands_sent == 1u,
             "runtime unicore config commands should dispatch without extra confirmation");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestRoverProfileGeneration(ctx);
  TestDiagnosticsProfileGeneration(ctx);
  TestPersistentAndSignalGroupSafety(ctx);
  TestFactoryResetProfileGeneration(ctx);
  TestInvalidDeferredInputs(ctx);
  TestRuntimeDispatcherBehavior(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver Unicore config profile builder tests passed\n";
  return EXIT_SUCCESS;
}
