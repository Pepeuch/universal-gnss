#pragma once

#include <cstdint>

#include "universal_gnss_driver/receiver_capabilities.hpp"
#include "universal_gnss_driver/receiver_command.hpp"

namespace universal_gnss_driver
{

enum class ReceiverConfigProfileKind : std::uint8_t
{
  kRover = 0,
  kBase = 1,
  kSurveyIn = 2,
  kNmeaOutput = 3,
  kRtcmOutput = 4,
  kDiagnosticsOutput = 5,
};

struct ReceiverConfigProfile
{
  ReceiverConfigProfileKind kind{ReceiverConfigProfileKind::kRover};
  const char* profile_id{""};
  const char* display_name{""};
  ProtocolSupportFlags required_input_protocols{0u};
  ProtocolSupportFlags required_output_protocols{0u};
  ReceiverFeatureFlags required_features{0u};
  ReceiverCommandSafetyLevel default_safety_level{ReceiverCommandSafetyLevel::kRuntime};
};

constexpr ReceiverConfigProfile GetReceiverConfigProfile(const ReceiverConfigProfileKind kind)
{
  switch (kind)
  {
    case ReceiverConfigProfileKind::kRover:
      return ReceiverConfigProfile{
          ReceiverConfigProfileKind::kRover,
          "rover",
          "Rover Mode",
          0u,
          0u,
          ToFlag(ReceiverFeature::kRoverMode),
          ReceiverCommandSafetyLevel::kRuntime,
      };
    case ReceiverConfigProfileKind::kBase:
      return ReceiverConfigProfile{
          ReceiverConfigProfileKind::kBase,
          "base",
          "Base Mode",
          0u,
          0u,
          ToFlag(ReceiverFeature::kBaseMode),
          ReceiverCommandSafetyLevel::kRuntime,
      };
    case ReceiverConfigProfileKind::kSurveyIn:
      return ReceiverConfigProfile{
          ReceiverConfigProfileKind::kSurveyIn,
          "survey_in",
          "Survey-In",
          0u,
          0u,
          ToFlag(ReceiverFeature::kBaseMode) | ToFlag(ReceiverFeature::kSurveyIn),
          ReceiverCommandSafetyLevel::kRuntime,
      };
    case ReceiverConfigProfileKind::kNmeaOutput:
      return ReceiverConfigProfile{
          ReceiverConfigProfileKind::kNmeaOutput,
          "nmea_output",
          "NMEA Output",
          0u,
          ToFlag(ReceiverProtocol::kNmea),
          0u,
          ReceiverCommandSafetyLevel::kRuntime,
      };
    case ReceiverConfigProfileKind::kRtcmOutput:
      return ReceiverConfigProfile{
          ReceiverConfigProfileKind::kRtcmOutput,
          "rtcm_output",
          "RTCM Output",
          0u,
          ToFlag(ReceiverProtocol::kRtcm3),
          0u,
          ReceiverCommandSafetyLevel::kRuntime,
      };
    case ReceiverConfigProfileKind::kDiagnosticsOutput:
      return ReceiverConfigProfile{
          ReceiverConfigProfileKind::kDiagnosticsOutput,
          "diagnostics_output",
          "Diagnostics Output",
          0u,
          0u,
          0u,
          ReceiverCommandSafetyLevel::kRuntime,
      };
  }

  return ReceiverConfigProfile{};
}

constexpr bool RequiresInputProtocol(const ReceiverConfigProfile& profile,
                                     const ReceiverProtocol protocol)
{
  return HasProtocolFlag(profile.required_input_protocols, protocol);
}

constexpr bool RequiresOutputProtocol(const ReceiverConfigProfile& profile,
                                      const ReceiverProtocol protocol)
{
  return HasProtocolFlag(profile.required_output_protocols, protocol);
}

constexpr bool RequiresReceiverFeature(const ReceiverConfigProfile& profile,
                                       const ReceiverFeature feature)
{
  return HasReceiverFeatureFlag(profile.required_features, feature);
}

constexpr bool CanApplyConfigProfile(const ReceiverCapabilities& capabilities,
                                     const ReceiverConfigProfile& profile)
{
  return (profile.required_input_protocols & ~capabilities.supported_input_protocols) == 0u &&
         (profile.required_output_protocols & ~capabilities.supported_output_protocols) == 0u &&
         (profile.required_features & ~capabilities.features) == 0u;
}

}  // namespace universal_gnss_driver
