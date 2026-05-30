#include "universal_gnss_driver/unicore_driver.hpp"

#include <utility>

#include "universal_gnss_driver/unicore_config_profile_builder.hpp"

namespace universal_gnss_driver
{

namespace
{

constexpr std::string_view kUnicoreFamily = "UM98x";

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
    const UnicoreConfigProfileBuildResult& build_result)
{
  ReceiverDriverProfileBuildResult result;
  result.profile_kind = profile_kind;

  if (build_result.status != UnicoreConfigProfileBuildStatus::kOk)
  {
    result.status = ReceiverDriverProfileBuildStatus::kBuildError;
    result.error_message = build_result.error_message;
    return result;
  }

  result.commands = build_result.commands;
  return result;
}

UnicorePersistenceTarget ResolvePersistence(const ReceiverCommandSafetyLevel safety_level)
{
  return safety_level == ReceiverCommandSafetyLevel::kPersistent
             ? UnicorePersistenceTarget::kSaveConfig
             : UnicorePersistenceTarget::kRuntimeOnly;
}

}  // namespace

UnicoreDriver::UnicoreDriver(UnicoreSessionConfig session_config)
    : session_(std::move(session_config))
{
}

ReceiverVendor UnicoreDriver::vendor() const
{
  return ReceiverVendor::kUnicore;
}

std::string_view UnicoreDriver::family() const
{
  return kUnicoreFamily;
}

const ReceiverCapabilities& UnicoreDriver::capabilities() const
{
  return DriverCapabilities();
}

const std::vector<ReceiverConfigProfileKind>& UnicoreDriver::supported_profiles() const
{
  return SupportedProfileKinds();
}

const universal_gnss::GnssRuntimeState& UnicoreDriver::current_state() const
{
  return session_.current_state();
}

void UnicoreDriver::FeedBytes(const std::uint8_t* data,
                              const std::size_t size,
                              const std::optional<std::int64_t> timestamp_ns)
{
  session_.FeedBytes(data, size, timestamp_ns);
}

void UnicoreDriver::FeedString(const std::string_view text,
                               const std::optional<std::int64_t> timestamp_ns)
{
  session_.FeedString(text, timestamp_ns);
}

void UnicoreDriver::Finalize()
{
  session_.Finalize();
}

void UnicoreDriver::Reset()
{
  session_.Reset();
}

ReceiverDriverProfileBuildResult UnicoreDriver::BuildRoverProfile(
    const ReceiverCommandSafetyLevel safety_level) const
{
  return BuildProfile(ReceiverConfigProfileKind::kRover, safety_level);
}

ReceiverDriverProfileBuildResult UnicoreDriver::BuildDiagnosticsProfile(
    const ReceiverCommandSafetyLevel safety_level) const
{
  return BuildProfile(ReceiverConfigProfileKind::kDiagnosticsOutput, safety_level);
}

const UnicoreSession& UnicoreDriver::session() const
{
  return session_;
}

const ReceiverCapabilities& UnicoreDriver::DriverCapabilities()
{
  static const ReceiverCapabilities capabilities = [] {
    ReceiverCapabilities value;
    AddSupportedInputProtocol(value, ReceiverProtocol::kRtcm3);
    AddSupportedInputProtocol(value, ReceiverProtocol::kUnicoreAscii);
    AddSupportedInputProtocol(value, ReceiverProtocol::kUnicoreBinary);
    AddSupportedOutputProtocol(value, ReceiverProtocol::kNmea);
    AddSupportedOutputProtocol(value, ReceiverProtocol::kRtcm3);
    AddSupportedOutputProtocol(value, ReceiverProtocol::kUnicoreAscii);
    AddSupportedOutputProtocol(value, ReceiverProtocol::kUnicoreBinary);
    AddReceiverFeature(value, ReceiverFeature::kRtk);
    AddReceiverFeature(value, ReceiverFeature::kHeading);
    AddReceiverFeature(value, ReceiverFeature::kDualAntenna);
    AddReceiverFeature(value, ReceiverFeature::kPps);
    AddReceiverFeature(value, ReceiverFeature::kRoverMode);
    AddReceiverFeature(value, ReceiverFeature::kSignalGroups);
    AddReceiverFeature(value, ReceiverFeature::kAsciiCommandConfig);
    return value;
  }();
  return capabilities;
}

const std::vector<ReceiverConfigProfileKind>& UnicoreDriver::SupportedProfileKinds()
{
  static const std::vector<ReceiverConfigProfileKind> supported{
      ReceiverConfigProfileKind::kRover,
      ReceiverConfigProfileKind::kDiagnosticsOutput,
  };
  return supported;
}

ReceiverDriverProfileBuildResult UnicoreDriver::BuildProfile(
    const ReceiverConfigProfileKind profile_kind,
    const ReceiverCommandSafetyLevel safety_level)
{
  if (safety_level == ReceiverCommandSafetyLevel::kFactoryReset)
  {
    return MakeUnsupportedSafetyResult(
        profile_kind,
        "Unicore driver does not support factory-reset profile generation");
  }

  UnicoreConfigProfile profile;
  const auto persistence = ResolvePersistence(safety_level);
  switch (profile_kind)
  {
    case ReceiverConfigProfileKind::kRover:
      profile = UnicoreConfigProfileBuilder::BuildUnicoreRoverProfile(persistence);
      break;
    case ReceiverConfigProfileKind::kDiagnosticsOutput:
      profile = UnicoreConfigProfileBuilder::BuildUnicoreDiagnosticsProfile(persistence);
      break;
    default:
    {
      ReceiverDriverProfileBuildResult result;
      result.status = ReceiverDriverProfileBuildStatus::kUnsupportedProfile;
      result.profile_kind = profile_kind;
      result.error_message = "Unicore driver does not support the requested profile";
      return result;
    }
  }

  return ConvertBuildResult(profile_kind, UnicoreConfigProfileBuilder::Build(profile));
}

}  // namespace universal_gnss_driver
