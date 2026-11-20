// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstdint>
#include <limits>
namespace ug::embedded
{
// External serialization is mandatory. Only the routing owner may acknowledge,
// after replacing its cached destination and finishing every previous callback.
// A publication ticket alone never permits releasing a callback context.
template <typename Callback>
class DestinationPublication
{
public:
  struct Target
  {
    Callback callback{};
    void* context{};
    std::uint64_t revision{};
  };
  std::uint64_t Publish(Callback callback, void* context)
  {
    if (desired_.revision == std::numeric_limits<std::uint64_t>::max())
      return 0;
    desired_ = {callback, context, desired_.revision + 1};
    return desired_.revision;
  }
  Target Desired() const
  {
    return desired_;
  }
  bool Acknowledge(std::uint64_t revision)
  {
    if (!revision || revision > desired_.revision || revision < acknowledged_)
      return false;
    acknowledged_ = revision;
    return true;
  }
  bool Applied(std::uint64_t ticket) const
  {
    return ticket && ticket <= acknowledged_;
  }

private:
  Target desired_{};
  std::uint64_t acknowledged_{};
};
}  // namespace ug::embedded
