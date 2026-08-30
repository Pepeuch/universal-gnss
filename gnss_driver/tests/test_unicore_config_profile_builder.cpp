#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command_dispatcher.hpp"
#include "universal_gnss_driver/unicore_config_profile_builder.hpp"
#include "universal_gnss_driver/unicore_model_profile.hpp"
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
using universal_gnss_driver::ResolveUnicoreModelProfile;
using universal_gnss_driver::UnicoreConfigProfile;
using universal_gnss_driver::UnicoreConfigProfileBuilder;
using universal_gnss_driver::UnicoreConfigProfileBuildStatus;
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
                       const std::string& text_prefix,
                       const std::string& expected_profile_id = "unicore_um98x_placeholder")
{
  ctx.Expect(command.kind == kind, "unicore command should preserve the expected command kind");
  ctx.Expect(command.safety_level == safety_level,
             "unicore command should preserve the expected safety level");
  ctx.Expect(command.target.vendor == ReceiverVendor::kUnicore &&
                 command.target.profile_id == expected_profile_id,
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
  ctx.Expect(
      result.commands.size() == 13u,
      "generic unicore rover helper should skip CONFIG SIGNALGROUP when the model is unknown");

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
  ctx.Expect(ContainsText(result.commands[2], "CONFIG RTK TIMEOUT 120"),
             "unicore rover helper should include the field-proven RTK correction-age window");
  ctx.Expect(ContainsText(result.commands[3], "CONFIG RTK RELIABILITY 3 1"),
             "unicore rover helper should retain the documented RTK reliability policy");
  ctx.Expect(ContainsText(result.commands[4], "CONFIG DGPS TIMEOUT 300"),
             "unicore rover helper should include the field-proven DGPS correction-age window");
  ctx.Expect(!ContainsText(result.commands[5], "CONFIG SIGNALGROUP"),
             "generic unicore rover helper should not guess a signal-group selection");
  ctx.Expect(!ContainsText(result.commands[5], "UNLOG"),
             "unicore rover helper should not emit UNLOG in the default runtime-safe profile");
  ctx.Expect(ContainsText(result.commands[5], "GPGGA 1"),
             "unicore rover helper should keep GPGGA available at a lighter 1 Hz rate using the "
             "documented current-port syntax");
  ctx.Expect(ContainsText(result.commands[6], "GPGSV 1"),
             "unicore rover helper should enable GPGSV so portable visibility and CN0 fallback "
             "stay available");
  ctx.Expect(
      ContainsText(result.commands[7], "GPGST 1"),
      "unicore rover helper should enable GPGST so portable accuracy fallback stays available");
  ctx.Expect(ContainsText(result.commands[8], "PVTSLNA 1"),
             "unicore rover helper should reduce PVTSLNA to a lighter 1 Hz fallback rate using "
             "the documented current-port syntax");
  ctx.Expect(ContainsText(result.commands[9], "BESTNAVA 0.2"),
             "unicore rover helper should emit BESTNAVA with direct-period syntax");
  ctx.Expect(ContainsText(result.commands[10], "RTKSTATUSA 1"),
             "unicore rover helper should emit RTKSTATUSA with direct-period syntax");
  ctx.Expect(ContainsText(result.commands[11], "RTCMSTATUSA ONCHANGED"),
             "unicore rover helper should emit RTCMSTATUSA with ONCHANGED syntax");
  ctx.Expect(
      ContainsText(result.commands[12], "SATSINFOA 1"),
      "unicore rover helper should keep SATSINFOA at 1 Hz for stable satellite observability");
}

void TestModelAwareRoverProfileGeneration(TestContext& ctx)
{
  {
    const auto profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(
        ResolveUnicoreModelProfile("UM960"), UnicorePersistenceTarget::kRuntimeOnly);
    const auto result = UnicoreConfigProfileBuilder::Build(profile);

    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk &&
                   result.commands.size() == 13u,
               "UM960 rover helper should stay known non-baseline and skip undocumented "
               "signal-group commands");
    ExpectTextCommand(ctx,
                      result.commands.front(),
                      ReceiverCommandKind::kApplyConfigProfile,
                      ReceiverCommandSafetyLevel::kRuntime,
                      "MODE ROVER SURVEY MOW",
                      "unicore_um960");
    ctx.Expect(std::none_of(result.commands.begin(),
                            result.commands.end(),
                            [](const ReceiverCommand& command)
                            {
                              return ContainsText(command, "CONFIG SIGNALGROUP");
                            }),
               "UM960 rover helper should not guess a signal-group selection");
  }

  {
    const auto profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(
        ResolveUnicoreModelProfile("UM980"), UnicorePersistenceTarget::kRuntimeOnly);
    const auto result = UnicoreConfigProfileBuilder::Build(profile);

    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk &&
                   result.commands.size() == 13u,
               "UM980 rover helper should keep the lean command count while selecting the "
               "kinematic UAV rover mode");
    ExpectTextCommand(ctx,
                      result.commands.front(),
                      ReceiverCommandKind::kApplyConfigProfile,
                      ReceiverCommandSafetyLevel::kRuntime,
                      "MODE ROVER UAV",
                      "unicore_um980");
    ctx.Expect(std::none_of(result.commands.begin(),
                            result.commands.end(),
                            [](const ReceiverCommand& command)
                            {
                              return ContainsText(command, "CONFIG SIGNALGROUP");
                            }),
               "UM980 rover helper should not guess a signal-group selection");
  }

  {
    const auto profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(
        ResolveUnicoreModelProfile("UB9A0"), UnicorePersistenceTarget::kRuntimeOnly);
    const auto result = UnicoreConfigProfileBuilder::Build(profile);

    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk &&
                   result.commands.size() == 13u,
               "UB9A0 rover helper should keep the lean command count while selecting the "
               "documented mower-oriented rover mode");
    ExpectTextCommand(ctx,
                      result.commands.front(),
                      ReceiverCommandKind::kApplyConfigProfile,
                      ReceiverCommandSafetyLevel::kRuntime,
                      "MODE ROVER SURVEY MOW",
                      "unicore_ub9a0");
    ctx.Expect(std::none_of(result.commands.begin(),
                            result.commands.end(),
                            [](const ReceiverCommand& command)
                            {
                              return ContainsText(command, "CONFIG SIGNALGROUP");
                            }),
               "UB9A0 rover helper should not guess a signal-group selection");
  }

  {
    const auto profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(
        ResolveUnicoreModelProfile("UM981"), UnicorePersistenceTarget::kRuntimeOnly);
    const auto result = UnicoreConfigProfileBuilder::Build(profile);

    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk &&
                   result.commands.size() == 13u,
               "UM981 rover helper should stay known non-baseline and skip undocumented "
               "signal-group commands");
    ctx.Expect(std::none_of(result.commands.begin(),
                            result.commands.end(),
                            [](const ReceiverCommand& command)
                            {
                              return ContainsText(command, "CONFIG SIGNALGROUP");
                            }),
               "UM981 rover helper should not guess a signal-group selection");
  }

  const auto profile =
      UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(ResolveUnicoreModelProfile("UM982"),
                                                            UnicorePersistenceTarget::kRuntimeOnly);
  const auto result = UnicoreConfigProfileBuilder::Build(profile);

  ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk && result.commands.size() == 14u,
             "UM982 rover helper should emit the documented dual-antenna signal-group command");
  ExpectTextCommand(ctx,
                    result.commands.front(),
                    ReceiverCommandKind::kApplyConfigProfile,
                    ReceiverCommandSafetyLevel::kRuntime,
                    "MODE ROVER SURVEY MOW",
                    "unicore_um982");
  ExpectTextCommand(ctx,
                    result.commands[5],
                    ReceiverCommandKind::kApplyConfigProfile,
                    ReceiverCommandSafetyLevel::kRuntime,
                    "CONFIG SIGNALGROUP 3 6",
                    "unicore_um982");
}

void TestDiagnosticsProfileGeneration(TestContext& ctx)
{
  const auto profile = UnicoreConfigProfileBuilder::BuildUnicoreDiagnosticsProfile();
  const auto result = UnicoreConfigProfileBuilder::Build(profile);

  ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk && result.commands.size() == 13u,
             "generic unicore diagnostics helper should preserve the lean rover command count "
             "without guessing a signal-group");
  ctx.Expect(!std::any_of(result.commands.begin(),
                          result.commands.end(),
                          [](const ReceiverCommand& command)
                          {
                            return ContainsText(command, "UNLOG");
                          }),
             "unicore diagnostics helper should not emit UNLOG by default");
  ctx.Expect(ContainsText(result.commands[8], "PVTSLNA 0.2"),
             "unicore diagnostics helper should restore PVTSLNA to 5 Hz for verbose live debugging "
             "using the documented current-port syntax");
  ctx.Expect(ContainsText(result.commands.back(), "SATSINFOA 1"),
             "unicore diagnostics helper should keep SATSINFOA at 1 Hz");
}

void TestUndocumentedPeriodicRateIsRejected(TestContext& ctx)
{
  auto profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile();
  for (auto& output : profile.output_messages)
  {
    if (output.message == UnicoreOutputMessageKind::kBestnava)
    {
      output.period_s = 1.0 / 7.0;
    }
  }

  const auto result = UnicoreConfigProfileBuilder::Build(profile);
  ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kInvalidArgument &&
                 result.error_message.find("documented period") != std::string::npos,
             "the Unicore builder should reject undocumented periodic output rates instead of "
             "emitting manual-incompatible commands");
}

void TestPersistentAndSignalGroupSafety(TestContext& ctx)
{
  {
    const auto profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(
        UnicorePersistenceTarget::kSaveConfig);
    const auto result = UnicoreConfigProfileBuilder::Build(profile);

    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk && !result.commands.empty(),
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
                      ReceiverCommandSafetyLevel::kRuntime,
                      "CONFIG SIGNALGROUP 3 6");

    MemoryByteSink sink;
    ReceiverCommandDispatcher dispatcher(sink);
    const auto accepted = dispatcher.Dispatch(result.commands.front());
    ctx.Expect(accepted.status == DispatchStatus::kSent,
               "runtime signal-group commands should remain immediately dispatchable without a "
               "persistent confirmation gate");
  }
}

void TestCom1BaudCommandGeneration(TestContext& ctx)
{
  auto profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile();
  profile.com1_baud_rate = 921600u;
  const auto result = UnicoreConfigProfileBuilder::Build(profile);

  ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk && result.commands.size() == 14u,
             "unicore COM1 baud injection should prepend one extra runtime command");
  ExpectTextCommand(ctx,
                    result.commands.front(),
                    ReceiverCommandKind::kApplyConfigProfile,
                    ReceiverCommandSafetyLevel::kRuntime,
                    "CONFIG COM1 921600 8 n 1");
}

void TestFactoryResetProfileGeneration(TestContext& ctx)
{
  const auto profile = UnicoreConfigProfileBuilder::BuildUnicoreFactoryResetProfile();
  const auto result = UnicoreConfigProfileBuilder::Build(profile);

  ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk && result.commands.size() == 1u,
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

  {
    auto profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile();
    profile.rtk_timeout_s = 0u;
    const auto result = UnicoreConfigProfileBuilder::Build(profile);
    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kInvalidArgument,
               "zero should be rejected instead of disabling RTK through a timeout profile");
  }

  {
    auto profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile();
    profile.dgps_timeout_s = 1801u;
    const auto result = UnicoreConfigProfileBuilder::Build(profile);
    ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kInvalidArgument,
               "DGPS correction-age values beyond the documented range should be rejected");
  }
}

void TestExplicitUavRuntimeMode(TestContext& ctx)
{
  UnicoreConfigProfile profile;
  profile.mode = UnicoreMode::kRoverUav;
  const auto result = UnicoreConfigProfileBuilder::Build(profile);

  ctx.Expect(result.status == UnicoreConfigProfileBuildStatus::kOk &&
                 result.commands.size() == 1u &&
                 ContainsText(result.commands.front(), "MODE ROVER UAV"),
             "the Unicore runtime builder should support MODE ROVER UAV");
}

void TestRuntimeDispatcherBehavior(TestContext& ctx)
{
  const auto result =
      UnicoreConfigProfileBuilder::Build(UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile());
  MemoryByteSink sink;
  ReceiverCommandDispatcher dispatcher(sink);

  const auto dispatch = dispatcher.Dispatch(result.commands.front());
  ctx.Expect(dispatch.status == DispatchStatus::kSent && dispatcher.metrics().commands_sent == 1u,
             "runtime unicore config commands should dispatch without extra confirmation");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestRoverProfileGeneration(ctx);
  TestModelAwareRoverProfileGeneration(ctx);
  TestDiagnosticsProfileGeneration(ctx);
  TestUndocumentedPeriodicRateIsRejected(ctx);
  TestPersistentAndSignalGroupSafety(ctx);
  TestCom1BaudCommandGeneration(ctx);
  TestFactoryResetProfileGeneration(ctx);
  TestInvalidDeferredInputs(ctx);
  TestExplicitUavRuntimeMode(ctx);
  TestRuntimeDispatcherBehavior(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver Unicore config profile builder tests passed\n";
  return EXIT_SUCCESS;
}
