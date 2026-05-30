#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_command_response.hpp"
#include "universal_gnss_driver/ubx_command_response_mapper.hpp"
#include "universal_gnss_protocols/ubx_cfg_builder.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"

namespace
{

using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandKind;
using universal_gnss_driver::ReceiverCommandResponseKind;
using universal_gnss_driver::UbxMessageIdentity;
using universal_gnss_protocols::UbxAckMessageKind;
using universal_gnss_protocols::UbxAckRecord;

struct TestContext
{
  int failures{0};

  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

ReceiverCommand MakeUbxCommand(const std::vector<std::uint8_t>& frame)
{
  ReceiverCommand command;
  command.kind = ReceiverCommandKind::kApplyConfigProfile;
  universal_gnss_driver::SetBinaryPayload(command, frame);
  return command;
}

ReceiverCommand MakeCfgValsetCommand()
{
  const auto result =
      universal_gnss_protocols::BuildUart1BaudrateFrame(115200u);
  if (result.status != universal_gnss_protocols::UbxCfgBuilderStatus::kOk)
  {
    std::cerr << "FAILED: test setup could not build UBX CFG-VALSET frame\n";
    std::exit(EXIT_FAILURE);
  }

  return MakeUbxCommand(result.frame);
}

void TestAckMapping(TestContext& ctx)
{
  const UbxAckRecord record{
      1111,
      UbxAckMessageKind::kAck,
      0x06u,
      0x8Au,
  };

  const auto response =
      universal_gnss_driver::MapUbxAckRecordToReceiverCommandResponse(record);
  ctx.Expect(response.kind == ReceiverCommandResponseKind::kAck &&
                 response.timestamp_ns == std::optional<std::int64_t>(1111),
             "ACK-ACK records should map to generic ACK responses");
  ctx.Expect(response.message.find("ACK-ACK") != std::string::npos &&
                 response.message.find("0x06") != std::string::npos &&
                 response.message.find("0x8A") != std::string::npos,
             "ACK response text should carry the UBX target class/id metadata");
}

void TestNakMapping(TestContext& ctx)
{
  const UbxAckRecord record{
      2222,
      UbxAckMessageKind::kNak,
      0x06u,
      0x8Bu,
  };

  const auto response =
      universal_gnss_driver::MapUbxAckRecordToReceiverCommandResponse(record);
  ctx.Expect(response.kind == ReceiverCommandResponseKind::kNak &&
                 response.timestamp_ns == std::optional<std::int64_t>(2222),
             "ACK-NAK records should map to generic NAK responses");
  ctx.Expect(response.message.find("ACK-NAK") != std::string::npos &&
                 response.message.find("0x06") != std::string::npos &&
                 response.message.find("0x8B") != std::string::npos,
             "NAK response text should carry the UBX target class/id metadata");
}

void TestCommandIdentityExtraction(TestContext& ctx)
{
  const ReceiverCommand command = MakeCfgValsetCommand();
  const auto identity =
      universal_gnss_driver::TryGetUbxCommandMessageIdentity(command);

  ctx.Expect(identity.has_value() &&
                 identity->class_id == 0x06u &&
                 identity->message_id == 0x8Au,
             "UBX command identity extraction should decode the outbound class/id");
}

void TestAckMatchesExpectedCommand(TestContext& ctx)
{
  const ReceiverCommand command = MakeCfgValsetCommand();
  const UbxAckRecord matching{
      3333,
      UbxAckMessageKind::kAck,
      0x06u,
      0x8Au,
  };
  const UbxAckRecord mismatched{
      3334,
      UbxAckMessageKind::kAck,
      0x06u,
      0x8Bu,
  };

  ctx.Expect(universal_gnss_driver::DoesUbxAckRecordMatchCommand(matching, command),
             "ACK target matching should succeed when the response class/id matches the command");
  ctx.Expect(!universal_gnss_driver::DoesUbxAckRecordMatchCommand(mismatched, command),
             "ACK target matching should fail when the response class/id differs from the command");
}

void TestInvalidCommandPayloadDoesNotMatch(TestContext& ctx)
{
  ReceiverCommand invalid_command;
  invalid_command.kind = ReceiverCommandKind::kRawBinary;
  universal_gnss_driver::SetBinaryPayload(invalid_command, {0x01u, 0x02u, 0x03u});

  const UbxAckRecord record{
      std::nullopt,
      UbxAckMessageKind::kAck,
      0x06u,
      0x8Au,
  };

  ctx.Expect(
      !universal_gnss_driver::TryGetUbxCommandMessageIdentity(invalid_command).has_value(),
      "non-UBX or structurally incomplete binary payloads should not expose a UBX command identity");
  ctx.Expect(!universal_gnss_driver::DoesUbxAckRecordMatchCommand(record, invalid_command),
             "ACK target matching should fail when the command payload is not a valid UBX frame");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestAckMapping(ctx);
  TestNakMapping(ctx);
  TestCommandIdentityExtraction(ctx);
  TestAckMatchesExpectedCommand(ctx);
  TestInvalidCommandPayloadDoesNotMatch(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver UBX command response mapper tests passed\n";
  return EXIT_SUCCESS;
}
