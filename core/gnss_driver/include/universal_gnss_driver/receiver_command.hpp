#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss_driver/receiver_profile.hpp"

namespace universal_gnss_driver
{

enum class ReceiverCommandKind : std::uint8_t
{
  kUnknown = 0,
  kApplyConfigProfile = 1,
  kSetProtocolOutputs = 2,
  kQuery = 3,
  kRawBinary = 4,
  kRawText = 5,
  kReset = 6,
};

enum class ReceiverCommandPayloadKind : std::uint8_t
{
  kNone = 0,
  kBinary = 1,
  kText = 2,
};

enum class ReceiverResponseKind : std::uint8_t
{
  kNone = 0,
  kAck = 1,
  kNack = 2,
  kBinaryPayload = 3,
  kTextPayload = 4,
  kStateUpdate = 5,
};

enum class ReceiverCommandSafetyLevel : std::uint8_t
{
  kRuntime = 0,
  kPersistent = 1,
  kFactoryReset = 2,
};

enum class ReceiverCommandFailurePolicy : std::uint8_t
{
  kAbortOnFailure = 0,
  kContinueOnFailure = 1,
};

struct ReceiverTargetSelector
{
  ReceiverVendor vendor{ReceiverVendor::kUnknown};
  std::string_view family{};
  std::string_view model{};
  std::string_view profile_id{};
};

struct ReceiverCommandRetryPolicy
{
  std::uint32_t timeout_ms{500u};
  std::uint8_t max_retries{0u};
};

struct ReceiverCommandPayload
{
  ReceiverCommandPayloadKind kind{ReceiverCommandPayloadKind::kNone};
  std::vector<std::uint8_t> binary{};
  std::string text{};
};

struct ReceiverCommand
{
  ReceiverCommandKind kind{ReceiverCommandKind::kUnknown};
  ReceiverTargetSelector target{};
  ReceiverResponseKind expected_response{ReceiverResponseKind::kAck};
  ReceiverCommandRetryPolicy retry_policy{};
  ReceiverCommandSafetyLevel safety_level{ReceiverCommandSafetyLevel::kRuntime};
  ReceiverCommandFailurePolicy failure_policy{ReceiverCommandFailurePolicy::kAbortOnFailure};
  bool explicit_safety_confirmation{false};
  ReceiverCommandPayload payload{};
};

constexpr bool RequiresExplicitSafetyConfirmation(const ReceiverCommandSafetyLevel safety_level)
{
  return safety_level == ReceiverCommandSafetyLevel::kPersistent ||
         safety_level == ReceiverCommandSafetyLevel::kFactoryReset;
}

constexpr bool HasSafeDispatchApproval(const ReceiverCommand& command)
{
  return !RequiresExplicitSafetyConfirmation(command.safety_level) ||
         command.explicit_safety_confirmation;
}

constexpr bool IsRequiredCommand(const ReceiverCommand& command)
{
  return command.failure_policy == ReceiverCommandFailurePolicy::kAbortOnFailure;
}

inline void SetBinaryPayload(ReceiverCommand& command, std::vector<std::uint8_t> payload)
{
  command.payload.kind = ReceiverCommandPayloadKind::kBinary;
  command.payload.binary = std::move(payload);
  command.payload.text.clear();
}

inline void SetTextPayload(ReceiverCommand& command, std::string payload)
{
  command.payload.kind = ReceiverCommandPayloadKind::kText;
  command.payload.text = std::move(payload);
  command.payload.binary.clear();
}

inline void ClearPayload(ReceiverCommand& command)
{
  command.payload = ReceiverCommandPayload{};
}

}  // namespace universal_gnss_driver
