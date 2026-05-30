#pragma once

#include <cstdint>
#include <type_traits>

#include "universal_gnss_driver/protocol_support.hpp"

namespace universal_gnss_driver
{

using ReceiverFeatureFlags = std::uint32_t;

enum class ReceiverFeature : ReceiverFeatureFlags
{
  kRtk = 1u << 0,
  kHeading = 1u << 1,
  kDualAntenna = 1u << 2,
  kRfMonitoring = 1u << 3,
  kPps = 1u << 4,
  kSurveyIn = 1u << 5,
  kBaseMode = 1u << 6,
  kRoverMode = 1u << 7,
  kConstellationConfig = 1u << 8,
  kCfgValset = 1u << 9,
  kSignalGroups = 1u << 10,
  kAsciiCommandConfig = 1u << 11,
};

static_assert(
    std::is_same<std::underlying_type<ReceiverFeature>::type, ReceiverFeatureFlags>::value,
    "ReceiverFeature must stay within a uint32_t flag set");

constexpr ReceiverFeatureFlags ToFlag(const ReceiverFeature feature)
{
  return static_cast<ReceiverFeatureFlags>(feature);
}

constexpr bool HasReceiverFeatureFlag(const ReceiverFeatureFlags flags,
                                      const ReceiverFeature feature)
{
  return (flags & ToFlag(feature)) != 0u;
}

constexpr ReceiverFeatureFlags SetReceiverFeatureFlag(const ReceiverFeatureFlags flags,
                                                      const ReceiverFeature feature)
{
  return static_cast<ReceiverFeatureFlags>(flags | ToFlag(feature));
}

constexpr ReceiverFeatureFlags ClearReceiverFeatureFlag(const ReceiverFeatureFlags flags,
                                                        const ReceiverFeature feature)
{
  return static_cast<ReceiverFeatureFlags>(flags & ~ToFlag(feature));
}

struct ReceiverCapabilities
{
  ProtocolSupportFlags supported_input_protocols{0};
  ProtocolSupportFlags supported_output_protocols{0};
  ReceiverFeatureFlags features{0};
};

constexpr bool SupportsInputProtocol(const ReceiverCapabilities& capabilities,
                                     const ReceiverProtocol protocol)
{
  return HasProtocolFlag(capabilities.supported_input_protocols, protocol);
}

constexpr bool SupportsOutputProtocol(const ReceiverCapabilities& capabilities,
                                      const ReceiverProtocol protocol)
{
  return HasProtocolFlag(capabilities.supported_output_protocols, protocol);
}

constexpr bool HasReceiverFeature(const ReceiverCapabilities& capabilities,
                                  const ReceiverFeature feature)
{
  return HasReceiverFeatureFlag(capabilities.features, feature);
}

inline void AddSupportedInputProtocol(ReceiverCapabilities& capabilities,
                                      const ReceiverProtocol protocol)
{
  capabilities.supported_input_protocols =
      SetProtocolFlag(capabilities.supported_input_protocols, protocol);
}

inline void AddSupportedOutputProtocol(ReceiverCapabilities& capabilities,
                                       const ReceiverProtocol protocol)
{
  capabilities.supported_output_protocols =
      SetProtocolFlag(capabilities.supported_output_protocols, protocol);
}

inline void AddReceiverFeature(ReceiverCapabilities& capabilities,
                               const ReceiverFeature feature)
{
  capabilities.features = SetReceiverFeatureFlag(capabilities.features, feature);
}

}  // namespace universal_gnss_driver
