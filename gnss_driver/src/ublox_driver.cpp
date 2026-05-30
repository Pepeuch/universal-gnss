#include "universal_gnss_driver/ublox_driver.hpp"

#include <utility>

#include "universal_gnss_driver/ublox_config_profile_builder.hpp"
#include "universal_gnss_protocols/ubx_cfg_builder.hpp"

namespace universal_gnss_driver
{

namespace
{

using universal_gnss_protocols::UbxCfgLayer;

constexpr std::string_view kUbloxFamily = "F9/F10";

std::vector<UbxCfgLayer> ResolveLayers(const ReceiverCommandSafetyLevel safety_level)
{
  if (safety_level == ReceiverCommandSafetyLevel::kPersistent)
  {
    return {UbxCfgLayer::kRam, UbxCfgLayer::kBbr};
  }

  return {UbxCfgLayer::kRam};
}

ReceiverDriverProfileBuildResult MakeUnsupportedSafetyResult(
    const ReceiverConfigProfileKind profile_kind,
    const char* error_message)
{
  ReceiverDriverProfileBuildResult result;
  result.status = ReceiverDriverProfileBuildStatus::kUnsupportedSafetyLevel;
  result.profile_kind = profile_kind;
  result.error_message = error_message;
  return result;
}

ReceiverDriverProfileBuildResult ConvertBuildResult(
    const ReceiverConfigProfileKind profile_kind,
    const UbloxConfigProfileBuildResult& build_result)
{
  ReceiverDriverProfileBuildResult result;
  result.profile_kind = profile_kind;

  if (build_result.status != UbloxConfigProfileBuildStatus::kOk)
  {
    result.status = ReceiverDriverProfileBuildStatus::kBuildError;
    result.error_message = build_result.error_message;
    return result;
  }

  result.commands = build_result.commands;
  return result;
}

}  // namespace

UbloxDriver::UbloxDriver(UbloxSessionConfig session_config)
    : session_(std::move(session_config))
{
}

ReceiverVendor UbloxDriver::vendor() const
{
  return ReceiverVendor::kUblox;
}

std::string_view UbloxDriver::family() const
{
  return kUbloxFamily;
}

const ReceiverCapabilities& UbloxDriver::capabilities() const
{
  return DriverCapabilities();
}

const std::vector<ReceiverConfigProfileKind>& UbloxDriver::supported_profiles() const
{
  return SupportedProfileKinds();
}

const universal_gnss::GnssRuntimeState& UbloxDriver::current_state() const
{
  return session_.current_state();
}

void UbloxDriver::FeedBytes(const std::uint8_t* data,
                            const std::size_t size,
                            const std::optional<std::int64_t> timestamp_ns)
{
  session_.FeedBytes(data, size, timestamp_ns);
}

void UbloxDriver::FeedString(const std::string_view text,
                             const std::optional<std::int64_t> timestamp_ns)
{
  session_.FeedString(text, timestamp_ns);
}

void UbloxDriver::Finalize()
{
  session_.Finalize();
}

void UbloxDriver::Reset()
{
  session_.Reset();
}

ReceiverDriverProfileBuildResult UbloxDriver::BuildRoverProfile(
    const ReceiverCommandSafetyLevel safety_level) const
{
  return BuildProfile(ReceiverConfigProfileKind::kRover, safety_level);
}

ReceiverDriverProfileBuildResult UbloxDriver::BuildDiagnosticsProfile(
    const ReceiverCommandSafetyLevel safety_level) const
{
  return BuildProfile(ReceiverConfigProfileKind::kDiagnosticsOutput, safety_level);
}

ReceiverDriverProfileBuildResult UbloxDriver::BuildBaseProfile(
    const ReceiverCommandSafetyLevel safety_level) const
{
  return BuildProfile(ReceiverConfigProfileKind::kBase, safety_level);
}

const UbloxSession& UbloxDriver::session() const
{
  return session_;
}

const ReceiverCapabilities& UbloxDriver::DriverCapabilities()
{
  static const ReceiverCapabilities capabilities = [] {
    ReceiverCapabilities value;
    AddSupportedInputProtocol(value, ReceiverProtocol::kUbx);
    AddSupportedInputProtocol(value, ReceiverProtocol::kRtcm3);
    AddSupportedOutputProtocol(value, ReceiverProtocol::kNmea);
    AddSupportedOutputProtocol(value, ReceiverProtocol::kUbx);
    AddReceiverFeature(value, ReceiverFeature::kRtk);
    AddReceiverFeature(value, ReceiverFeature::kRfMonitoring);
    AddReceiverFeature(value, ReceiverFeature::kPps);
    AddReceiverFeature(value, ReceiverFeature::kBaseMode);
    AddReceiverFeature(value, ReceiverFeature::kRoverMode);
    AddReceiverFeature(value, ReceiverFeature::kConstellationConfig);
    AddReceiverFeature(value, ReceiverFeature::kCfgValset);
    return value;
  }();
  return capabilities;
}

const std::vector<ReceiverConfigProfileKind>& UbloxDriver::SupportedProfileKinds()
{
  static const std::vector<ReceiverConfigProfileKind> supported{
      ReceiverConfigProfileKind::kRover,
      ReceiverConfigProfileKind::kDiagnosticsOutput,
      ReceiverConfigProfileKind::kBase,
  };
  return supported;
}

ReceiverDriverProfileBuildResult UbloxDriver::BuildProfile(
    const ReceiverConfigProfileKind profile_kind,
    const ReceiverCommandSafetyLevel safety_level)
{
  if (safety_level == ReceiverCommandSafetyLevel::kFactoryReset)
  {
    return MakeUnsupportedSafetyResult(
        profile_kind,
        "u-blox driver does not support factory-reset profile generation");
  }

  std::vector<UbxCfgLayer> layers = ResolveLayers(safety_level);
  UbloxConfigProfile profile;
  switch (profile_kind)
  {
    case ReceiverConfigProfileKind::kRover:
      profile = UbloxConfigProfileBuilder::BuildUbloxRoverProfile(safety_level, layers);
      break;
    case ReceiverConfigProfileKind::kDiagnosticsOutput:
      profile = UbloxConfigProfileBuilder::BuildUbloxDiagnosticsProfile(safety_level, layers);
      break;
    case ReceiverConfigProfileKind::kBase:
      profile = UbloxConfigProfileBuilder::BuildUbloxBaseProfile(safety_level, layers);
      break;
    default:
    {
      ReceiverDriverProfileBuildResult result;
      result.status = ReceiverDriverProfileBuildStatus::kUnsupportedProfile;
      result.profile_kind = profile_kind;
      result.error_message = "u-blox driver does not support the requested profile";
      return result;
    }
  }

  return ConvertBuildResult(profile_kind, UbloxConfigProfileBuilder::Build(profile));
}

}  // namespace universal_gnss_driver
