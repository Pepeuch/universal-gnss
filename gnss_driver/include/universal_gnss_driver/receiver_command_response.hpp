#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace universal_gnss_driver
{

using ReceiverCommandTimestampNs = std::int64_t;

enum class ReceiverCommandResponseKind : std::uint8_t
{
  kNone = 0,
  kAck = 1,
  kNak = 2,
  kTextOk = 3,
  kTextError = 4,
  kTimeout = 5,
};

struct ReceiverCommandResponse
{
  ReceiverCommandResponseKind kind{ReceiverCommandResponseKind::kNone};
  std::optional<ReceiverCommandTimestampNs> timestamp_ns{};
  std::string message{};
};

constexpr bool IsPositiveReceiverCommandResponseKind(
    const ReceiverCommandResponseKind kind)
{
  return kind == ReceiverCommandResponseKind::kAck ||
         kind == ReceiverCommandResponseKind::kTextOk;
}

constexpr bool IsNegativeReceiverCommandResponseKind(
    const ReceiverCommandResponseKind kind)
{
  return kind == ReceiverCommandResponseKind::kNak ||
         kind == ReceiverCommandResponseKind::kTextError ||
         kind == ReceiverCommandResponseKind::kTimeout;
}

inline void ClearReceiverCommandResponse(ReceiverCommandResponse& response)
{
  response = ReceiverCommandResponse{};
}

}  // namespace universal_gnss_driver
