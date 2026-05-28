#pragma once

#include <cstdint>

#include "universal_gnss_driver/receiver_capabilities.hpp"

namespace universal_gnss_driver
{

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
};

}  // namespace universal_gnss_driver
