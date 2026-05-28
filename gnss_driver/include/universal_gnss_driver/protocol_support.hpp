#pragma once

#include <cstdint>
#include <type_traits>

namespace universal_gnss_driver
{

using ProtocolSupportFlags = std::uint32_t;

enum class ReceiverProtocol : ProtocolSupportFlags
{
  kNmea = 1u << 0,
  kUbx = 1u << 1,
  kRtcm3 = 1u << 2,
  kUnicoreAscii = 1u << 3,
  kUnicoreBinary = 1u << 4,
};

static_assert(
    std::is_same<std::underlying_type<ReceiverProtocol>::type, ProtocolSupportFlags>::value,
    "ReceiverProtocol must stay within a uint32_t flag set");

constexpr ProtocolSupportFlags ToFlag(const ReceiverProtocol protocol)
{
  return static_cast<ProtocolSupportFlags>(protocol);
}

constexpr bool HasProtocolFlag(const ProtocolSupportFlags flags, const ReceiverProtocol protocol)
{
  return (flags & ToFlag(protocol)) != 0u;
}

constexpr ProtocolSupportFlags SetProtocolFlag(const ProtocolSupportFlags flags,
                                               const ReceiverProtocol protocol)
{
  return static_cast<ProtocolSupportFlags>(flags | ToFlag(protocol));
}

constexpr ProtocolSupportFlags ClearProtocolFlag(const ProtocolSupportFlags flags,
                                                 const ReceiverProtocol protocol)
{
  return static_cast<ProtocolSupportFlags>(flags & ~ToFlag(protocol));
}

}  // namespace universal_gnss_driver
