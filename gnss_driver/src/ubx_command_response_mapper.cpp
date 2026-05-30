#include "universal_gnss_driver/ubx_command_response_mapper.hpp"

#include <cstddef>
#include <string>

namespace universal_gnss_driver
{

namespace
{

constexpr std::uint8_t kUbxSync1 = 0xB5u;
constexpr std::uint8_t kUbxSync2 = 0x62u;
constexpr std::size_t kUbxHeaderSize = 6u;
constexpr std::size_t kUbxChecksumSize = 2u;

std::string HexByte(const std::uint8_t value)
{
  constexpr char kHexDigits[] = "0123456789ABCDEF";

  std::string text = "0x00";
  text[2] = kHexDigits[(value >> 4u) & 0x0Fu];
  text[3] = kHexDigits[value & 0x0Fu];
  return text;
}

std::string BuildAckMessage(const universal_gnss_protocols::UbxAckRecord& record)
{
  std::string text = record.kind == universal_gnss_protocols::UbxAckMessageKind::kAck
                         ? "UBX ACK-ACK for "
                         : "UBX ACK-NAK for ";
  text += HexByte(record.target_class_id);
  text += "/";
  text += HexByte(record.target_message_id);
  return text;
}

}  // namespace

std::optional<UbxMessageIdentity> TryGetUbxCommandMessageIdentity(
    const ReceiverCommand& command)
{
  if (command.payload.kind != ReceiverCommandPayloadKind::kBinary)
  {
    return std::nullopt;
  }

  const auto& bytes = command.payload.binary;
  if (bytes.size() < (kUbxHeaderSize + kUbxChecksumSize) ||
      bytes[0] != kUbxSync1 ||
      bytes[1] != kUbxSync2)
  {
    return std::nullopt;
  }

  const std::size_t payload_size =
      static_cast<std::size_t>(bytes[4u]) |
      (static_cast<std::size_t>(bytes[5u]) << 8u);
  const std::size_t expected_frame_size =
      kUbxHeaderSize + payload_size + kUbxChecksumSize;
  if (bytes.size() != expected_frame_size)
  {
    return std::nullopt;
  }

  return UbxMessageIdentity{bytes[2u], bytes[3u]};
}

ReceiverCommandResponse MapUbxAckRecordToReceiverCommandResponse(
    const universal_gnss_protocols::UbxAckRecord& record)
{
  ReceiverCommandResponse response;
  response.kind = record.kind == universal_gnss_protocols::UbxAckMessageKind::kAck
                      ? ReceiverCommandResponseKind::kAck
                      : ReceiverCommandResponseKind::kNak;
  response.timestamp_ns = record.timestamp_ns;
  response.message = BuildAckMessage(record);
  return response;
}

bool DoesUbxAckRecordMatchCommand(const universal_gnss_protocols::UbxAckRecord& record,
                                  const ReceiverCommand& command)
{
  const auto command_identity = TryGetUbxCommandMessageIdentity(command);
  if (!command_identity.has_value())
  {
    return false;
  }

  return command_identity->class_id == record.target_class_id &&
         command_identity->message_id == record.target_message_id;
}

}  // namespace universal_gnss_driver
