#include <cstdlib>
#include <iostream>
#include <string>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_config_profile.hpp"
#include "universal_gnss_driver/receiver_profiles.hpp"

namespace
{

using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandKind;
using universal_gnss_driver::ReceiverCommandPayloadKind;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
using universal_gnss_driver::ReceiverConfigProfileKind;
using universal_gnss_driver::ReceiverFeature;
using universal_gnss_driver::ReceiverProtocol;
using universal_gnss_driver::ReceiverProfile;

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

const ReceiverProfile& RequireProfile(TestContext& ctx, const std::string& profile_id)
{
  const ReceiverProfile* profile =
      universal_gnss_driver::FindBuiltInReceiverProfile(profile_id);
  ctx.Expect(profile != nullptr, "expected built-in receiver profile: " + profile_id);
  if (profile == nullptr)
  {
    std::cerr << "FAILED: missing required test profile, aborting\n";
    std::exit(EXIT_FAILURE);
  }

  return *profile;
}

void TestDefaultCommandModel(TestContext& ctx)
{
  const ReceiverCommand command{};

  ctx.Expect(command.kind == ReceiverCommandKind::kUnknown,
             "default receiver command kind should stay unknown");
  ctx.Expect(command.safety_level == ReceiverCommandSafetyLevel::kRuntime &&
                 !universal_gnss_driver::RequiresExplicitSafetyConfirmation(command.safety_level),
             "default receiver command safety should stay runtime-only");
  ctx.Expect(command.retry_policy.timeout_ms == 500u &&
                 command.retry_policy.max_retries == 0u,
             "default receiver command retry/timeout policy should stay conservative");
  ctx.Expect(command.payload.kind == ReceiverCommandPayloadKind::kNone &&
                 command.payload.binary.empty() &&
                 command.payload.text.empty(),
             "default receiver command should not generate vendor payload automatically");
}

void TestSafetyAcknowledgementPolicy(TestContext& ctx)
{
  ReceiverCommand persistent{};
  persistent.safety_level = ReceiverCommandSafetyLevel::kPersistent;

  ctx.Expect(universal_gnss_driver::RequiresExplicitSafetyConfirmation(
                 persistent.safety_level) &&
                 !universal_gnss_driver::HasSafeDispatchApproval(persistent),
             "persistent commands should require explicit safety confirmation");

  persistent.explicit_safety_confirmation = true;
  ctx.Expect(universal_gnss_driver::HasSafeDispatchApproval(persistent),
             "persistent commands should become dispatchable after explicit confirmation");

  ReceiverCommand factory_reset{};
  factory_reset.safety_level = ReceiverCommandSafetyLevel::kFactoryReset;
  ctx.Expect(universal_gnss_driver::RequiresExplicitSafetyConfirmation(
                 factory_reset.safety_level) &&
                 !universal_gnss_driver::HasSafeDispatchApproval(factory_reset),
             "factory reset commands should require explicit safety confirmation");
}

void TestConfigProfileDeclarations(TestContext& ctx)
{
  const auto rover =
      universal_gnss_driver::GetReceiverConfigProfile(ReceiverConfigProfileKind::kRover);
  const auto base =
      universal_gnss_driver::GetReceiverConfigProfile(ReceiverConfigProfileKind::kBase);
  const auto survey_in =
      universal_gnss_driver::GetReceiverConfigProfile(ReceiverConfigProfileKind::kSurveyIn);
  const auto nmea_output =
      universal_gnss_driver::GetReceiverConfigProfile(ReceiverConfigProfileKind::kNmeaOutput);
  const auto rtcm_output =
      universal_gnss_driver::GetReceiverConfigProfile(ReceiverConfigProfileKind::kRtcmOutput);
  const auto diagnostics_output = universal_gnss_driver::GetReceiverConfigProfile(
      ReceiverConfigProfileKind::kDiagnosticsOutput);

  ctx.Expect(universal_gnss_driver::RequiresReceiverFeature(rover, ReceiverFeature::kRoverMode) &&
                 !universal_gnss_driver::RequiresReceiverFeature(
                     rover, ReceiverFeature::kBaseMode),
             "rover profile should require rover-mode support only");
  ctx.Expect(universal_gnss_driver::RequiresReceiverFeature(base, ReceiverFeature::kBaseMode),
             "base profile should require base-mode support");
  ctx.Expect(universal_gnss_driver::RequiresReceiverFeature(
                 survey_in, ReceiverFeature::kBaseMode) &&
                 universal_gnss_driver::RequiresReceiverFeature(
                     survey_in, ReceiverFeature::kSurveyIn),
             "survey-in profile should require both base-mode and survey-in support");
  ctx.Expect(universal_gnss_driver::RequiresOutputProtocol(
                 nmea_output, ReceiverProtocol::kNmea) &&
                 universal_gnss_driver::RequiresOutputProtocol(
                     rtcm_output, ReceiverProtocol::kRtcm3),
             "output-oriented profiles should declare their required output protocols");
  ctx.Expect(diagnostics_output.default_safety_level ==
                 ReceiverCommandSafetyLevel::kRuntime &&
                 diagnostics_output.required_features == 0u,
             "diagnostics output profile should stay generic and runtime-safe by default");
}

void TestConfigProfilesAgainstReceiverProfiles(TestContext& ctx)
{
  const auto rover =
      universal_gnss_driver::GetReceiverConfigProfile(ReceiverConfigProfileKind::kRover);
  const auto base =
      universal_gnss_driver::GetReceiverConfigProfile(ReceiverConfigProfileKind::kBase);
  const auto survey_in =
      universal_gnss_driver::GetReceiverConfigProfile(ReceiverConfigProfileKind::kSurveyIn);
  const auto nmea_output =
      universal_gnss_driver::GetReceiverConfigProfile(ReceiverConfigProfileKind::kNmeaOutput);
  const auto rtcm_output =
      universal_gnss_driver::GetReceiverConfigProfile(ReceiverConfigProfileKind::kRtcmOutput);

  const ReceiverProfile& generic = RequireProfile(ctx, "generic_nmea");
  const ReceiverProfile& ublox = RequireProfile(ctx, "ublox_f9_f10");
  const ReceiverProfile& unicore = RequireProfile(ctx, "unicore_um98x_placeholder");
  const ReceiverProfile& unicore_um982 = RequireProfile(ctx, "unicore_um982");

  ctx.Expect(universal_gnss_driver::CanApplyConfigProfile(generic.capabilities, rover) &&
                 universal_gnss_driver::CanApplyConfigProfile(generic.capabilities, nmea_output) &&
                 !universal_gnss_driver::CanApplyConfigProfile(generic.capabilities, rtcm_output),
             "generic NMEA profile should support rover and NMEA output but not RTCM output");
  ctx.Expect(universal_gnss_driver::CanApplyConfigProfile(ublox.capabilities, rover) &&
                 universal_gnss_driver::CanApplyConfigProfile(ublox.capabilities, nmea_output) &&
                 !universal_gnss_driver::CanApplyConfigProfile(ublox.capabilities, base) &&
                 !universal_gnss_driver::CanApplyConfigProfile(ublox.capabilities, survey_in),
             "u-blox family profile should stay conservative about base and survey-in config");
  ctx.Expect(universal_gnss_driver::CanApplyConfigProfile(unicore.capabilities, base) &&
                 universal_gnss_driver::CanApplyConfigProfile(unicore.capabilities, survey_in) &&
                 universal_gnss_driver::CanApplyConfigProfile(unicore.capabilities, rtcm_output),
             "Unicore placeholder should advertise base, survey-in, and RTCM output support");
  ctx.Expect(universal_gnss_driver::CanApplyConfigProfile(unicore_um982.capabilities, rover) &&
                 universal_gnss_driver::CanApplyConfigProfile(unicore_um982.capabilities, rtcm_output),
             "model-specific UM982 profiles should remain compatible with rover and RTCM-output generic intents");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestDefaultCommandModel(ctx);
  TestSafetyAcknowledgementPolicy(ctx);
  TestConfigProfileDeclarations(ctx);
  TestConfigProfilesAgainstReceiverProfiles(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver command model tests passed\n";
  return EXIT_SUCCESS;
}
