#include "universal_gnss_driver/receiver_profiles.hpp"

namespace universal_gnss_driver
{

namespace
{

constexpr ReceiverCapabilities MakeCapabilities(const ProtocolSupportFlags input_protocols,
                                                const ProtocolSupportFlags output_protocols,
                                                const ReceiverFeatureFlags features)
{
  return ReceiverCapabilities{input_protocols, output_protocols, features};
}

constexpr ReceiverProfile kBuiltInProfiles[] = {
    ReceiverProfile{
        "generic_nmea",
        "Generic NMEA Receiver",
        ReceiverVendor::kGeneric,
        "Generic",
        "NMEA",
        false,
        MakeCapabilities(0u,
                         ToFlag(ReceiverProtocol::kNmea),
                         ToFlag(ReceiverFeature::kRoverMode)),
    },
    ReceiverProfile{
        "ublox_f9_f10",
        "u-blox F9/F10 Family",
        ReceiverVendor::kUblox,
        "F9/F10",
        "family",
        false,
        MakeCapabilities(ToFlag(ReceiverProtocol::kUbx) |
                             ToFlag(ReceiverProtocol::kRtcm3),
                         ToFlag(ReceiverProtocol::kNmea) |
                             ToFlag(ReceiverProtocol::kUbx),
                         ToFlag(ReceiverFeature::kRfMonitoring) |
                             ToFlag(ReceiverFeature::kPps) |
                             ToFlag(ReceiverFeature::kRoverMode)),
    },
    ReceiverProfile{
        "unicore_um98x_placeholder",
        "Unicore UM98x Placeholder",
        ReceiverVendor::kUnicore,
        "UM98x",
        "placeholder",
        true,
        MakeCapabilities(ToFlag(ReceiverProtocol::kRtcm3) |
                             ToFlag(ReceiverProtocol::kUnicoreAscii) |
                             ToFlag(ReceiverProtocol::kUnicoreBinary),
                         ToFlag(ReceiverProtocol::kNmea) |
                             ToFlag(ReceiverProtocol::kRtcm3) |
                             ToFlag(ReceiverProtocol::kUnicoreAscii) |
                             ToFlag(ReceiverProtocol::kUnicoreBinary),
                         ToFlag(ReceiverFeature::kRtk) |
                             ToFlag(ReceiverFeature::kHeading) |
                             ToFlag(ReceiverFeature::kDualAntenna) |
                             ToFlag(ReceiverFeature::kPps) |
                             ToFlag(ReceiverFeature::kSurveyIn) |
                             ToFlag(ReceiverFeature::kBaseMode) |
                             ToFlag(ReceiverFeature::kRoverMode)),
    },
    ReceiverProfile{
        "quectel_placeholder",
        "Quectel Placeholder",
        ReceiverVendor::kQuectel,
        "Quectel",
        "placeholder",
        true,
        MakeCapabilities(ToFlag(ReceiverProtocol::kRtcm3),
                         ToFlag(ReceiverProtocol::kNmea),
                         ToFlag(ReceiverFeature::kRtk) |
                             ToFlag(ReceiverFeature::kPps) |
                             ToFlag(ReceiverFeature::kRoverMode)),
    },
};

}  // namespace

const std::array<ReceiverProfile, 4>& GetBuiltInReceiverProfiles()
{
  static const std::array<ReceiverProfile, 4> profiles = {
      kBuiltInProfiles[0],
      kBuiltInProfiles[1],
      kBuiltInProfiles[2],
      kBuiltInProfiles[3],
  };
  return profiles;
}

const ReceiverProfile* FindBuiltInReceiverProfile(const std::string_view profile_id)
{
  const auto& profiles = GetBuiltInReceiverProfiles();
  for (const auto& profile : profiles)
  {
    if (profile.profile_id == profile_id)
    {
      return &profile;
    }
  }

  return nullptr;
}

}  // namespace universal_gnss_driver
