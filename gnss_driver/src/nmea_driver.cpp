#include "universal_gnss_driver/nmea_driver.hpp"

#include <utility>

namespace universal_gnss_driver
{

namespace
{

constexpr std::string_view kNmeaFamily = "NMEA";

}  // namespace

NmeaDriver::NmeaDriver(NmeaSessionConfig session_config) : session_(std::move(session_config))
{
}

ReceiverVendor NmeaDriver::vendor() const
{
  return ReceiverVendor::kGeneric;
}

std::string_view NmeaDriver::family() const
{
  return kNmeaFamily;
}

const ReceiverCapabilities& NmeaDriver::capabilities() const
{
  return DriverCapabilities();
}

const std::vector<ReceiverConfigProfileKind>& NmeaDriver::supported_profiles() const
{
  return SupportedProfileKinds();
}

const universal_gnss::GnssRuntimeState& NmeaDriver::current_state() const
{
  return session_.current_state();
}

void NmeaDriver::FeedBytes(const std::uint8_t* data,
                           const std::size_t size,
                           const std::optional<std::int64_t> timestamp_ns)
{
  session_.FeedBytes(data, size, timestamp_ns);
}

void NmeaDriver::FeedString(const std::string_view text,
                            const std::optional<std::int64_t> timestamp_ns)
{
  session_.FeedString(text, timestamp_ns);
}

void NmeaDriver::Finalize()
{
  session_.Finalize();
}

void NmeaDriver::Reset()
{
  session_.Reset();
}

ReceiverDriverProfileBuildResult NmeaDriver::BuildRoverProfile(
    const ReceiverCommandSafetyLevel safety_level) const
{
  (void)safety_level;
  return MakeUnsupportedProfileResult(ReceiverConfigProfileKind::kRover);
}

ReceiverDriverProfileBuildResult NmeaDriver::BuildDiagnosticsProfile(
    const ReceiverCommandSafetyLevel safety_level) const
{
  (void)safety_level;
  return MakeUnsupportedProfileResult(ReceiverConfigProfileKind::kDiagnosticsOutput);
}

const NmeaSession& NmeaDriver::session() const
{
  return session_;
}

const ReceiverCapabilities& NmeaDriver::DriverCapabilities()
{
  static const ReceiverCapabilities capabilities = [] {
    ReceiverCapabilities value;
    AddSupportedOutputProtocol(value, ReceiverProtocol::kNmea);
    AddReceiverFeature(value, ReceiverFeature::kRoverMode);
    return value;
  }();
  return capabilities;
}

const std::vector<ReceiverConfigProfileKind>& NmeaDriver::SupportedProfileKinds()
{
  static const std::vector<ReceiverConfigProfileKind> supported{};
  return supported;
}

ReceiverDriverProfileBuildResult NmeaDriver::MakeUnsupportedProfileResult(
    const ReceiverConfigProfileKind profile_kind)
{
  ReceiverDriverProfileBuildResult result;
  result.status = ReceiverDriverProfileBuildStatus::kUnsupportedProfile;
  result.profile_kind = profile_kind;
  result.error_message = "generic NMEA driver does not support configuration profile generation";
  return result;
}

}  // namespace universal_gnss_driver
