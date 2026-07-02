#include "universal_gnss_driver/receiver_profiles.hpp"

#include "universal_gnss_driver/unicore_model_profile.hpp"

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

ReceiverProfile BuildUnicoreProfile(const UnicoreModelProfile& model_profile,
                                    const char* display_name)
{
  return ReceiverProfile{
      model_profile.profile_id,
      display_name,
      ReceiverVendor::kUnicore,
      model_profile.family,
      model_profile.model,
      model_profile.placeholder,
      model_profile.capabilities,
  };
}

}  // namespace

const std::array<ReceiverProfile, 9>& GetBuiltInReceiverProfiles()
{
  static const std::array<ReceiverProfile, 9> profiles = {
      ReceiverProfile{
          "generic_nmea",
          "Generic NMEA Receiver",
          ReceiverVendor::kGeneric,
          "Generic",
          "NMEA",
          false,
          MakeCapabilities(0u,
                           ToFlag(ReceiverProtocol::kNmea),
                           ToFlag(ReceiverFeature::kRtk) |
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
      BuildUnicoreProfile(ResolveUnicoreModelProfile(), "Unicore N4 Generic"),
      BuildUnicoreProfile(ResolveUnicoreModelProfile("UM960"), "Unicore UM960"),
      BuildUnicoreProfile(ResolveUnicoreModelProfile("UM980"), "Unicore UM980"),
      BuildUnicoreProfile(ResolveUnicoreModelProfile("UM981"), "Unicore UM981"),
      BuildUnicoreProfile(ResolveUnicoreModelProfile("UM982"), "Unicore UM982"),
      BuildUnicoreProfile(ResolveUnicoreModelProfile("UB9A0"), "Unicore UB9A0"),
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
