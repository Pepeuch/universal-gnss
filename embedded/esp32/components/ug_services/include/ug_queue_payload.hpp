// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ug::embedded
{
// One boot-time owner. Only payload bytes live here, never FreeRTOS queue control.
// Once published to a queue, storage is retained until restart: no live release,
// reallocation, fallback to another memory class, or change of byte capacity.
template <std::size_t Capacity>
class QueuePayload
{
public:
  QueuePayload() = default;
  QueuePayload(const QueuePayload&) = delete;
  QueuePayload& operator=(const QueuePayload&) = delete;
  bool Reserve(void* (*allocate)(std::size_t))
  {
    if (data_)
      return true;
    if (!allocate)
      return false;
    auto* bytes = static_cast<std::uint8_t*>(allocate(Capacity));
    if (!bytes)
      return false;
    std::memset(bytes, 0, Capacity);
    data_ = bytes;
    return true;
  }
  std::uint8_t* data() const
  {
    return data_;
  }
  static constexpr std::size_t size()
  {
    return Capacity;
  }

private:
  std::uint8_t* data_{};
};
}  // namespace ug::embedded
