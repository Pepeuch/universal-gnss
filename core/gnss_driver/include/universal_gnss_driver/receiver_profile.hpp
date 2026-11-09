#pragma once

#include <cstdint>

#include "universal_gnss_driver/receiver_capabilities.hpp"

namespace universal_gnss_driver
{

enum class ReceiverConfigProfileKind : std::uint8_t;

using ReceiverConfigProfileFlags = std::uint32_t;

constexpr ReceiverConfigProfileFlags ToConfigProfileFlag(const ReceiverConfigProfileKind kind)
{
  return ReceiverConfigProfileFlags{1u} << static_cast<std::uint8_t>(kind);
}

enum class ReceiverVendor : std::uint8_t
{
  kUnknown = 0,
  kGeneric = 1,
  kUblox = 2,
  kUnicore = 3,
  kQuectel = 4,
};

struct ReceiverProfile
{
  const char* profile_id{""};
  const char* display_name{""};
  ReceiverVendor vendor{ReceiverVendor::kUnknown};
  const char* family{""};
  const char* model{""};
  bool placeholder{false};
  ReceiverCapabilities capabilities{};
  ReceiverConfigProfileFlags supported_config_profiles{0u};

  bool SupportsConfigProfile(const ReceiverConfigProfileKind kind) const
  {
    return (supported_config_profiles & ToConfigProfileFlag(kind)) != 0u;
  }
};

}  // namespace universal_gnss_driver
