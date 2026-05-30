#pragma once

#include <cstdint>
#include <optional>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_command_response.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"

namespace universal_gnss_driver
{

struct UbxMessageIdentity
{
  std::uint8_t class_id{0};
  std::uint8_t message_id{0};
};

std::optional<UbxMessageIdentity> TryGetUbxCommandMessageIdentity(
    const ReceiverCommand& command);

ReceiverCommandResponse MapUbxAckRecordToReceiverCommandResponse(
    const universal_gnss_protocols::UbxAckRecord& record);

bool DoesUbxAckRecordMatchCommand(const universal_gnss_protocols::UbxAckRecord& record,
                                  const ReceiverCommand& command);

}  // namespace universal_gnss_driver
