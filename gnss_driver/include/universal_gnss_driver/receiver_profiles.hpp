#pragma once

#include <array>
#include <string_view>

#include "universal_gnss_driver/receiver_profile.hpp"

namespace universal_gnss_driver
{

const std::array<ReceiverProfile, 7>& GetBuiltInReceiverProfiles();

const ReceiverProfile* FindBuiltInReceiverProfile(std::string_view profile_id);

}  // namespace universal_gnss_driver
