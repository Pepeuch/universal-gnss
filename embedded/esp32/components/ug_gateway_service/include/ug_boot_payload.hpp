// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
#include <new>

namespace ug::embedded
{
// Startup-owner only. Construct before publishing any queue/task pointer; the
// object stays at its first address for the whole boot, even after SDK failure.
// This class never frees, retries an allocation, or falls back to another heap.
// T's constructor must not allocate; the production routing constructor is
// exercised by the host startup test as well as the pinned SDK build.
template <class T>
class BootPayload
{
public:
  BootPayload() = default;
  BootPayload(const BootPayload&) = delete;
  BootPayload& operator=(const BootPayload&) = delete;
  bool Initialize(void* (*allocate)(std::size_t))
  {
    if (attempted_)
      return value_ != nullptr;
    attempted_ = true;
    if (!allocate)
      return false;
    auto* memory = allocate(sizeof(T));
    if (!memory)
      return false;
    value_ = new (memory) T{};
    return true;
  }
  T* Get() const
  {
    return value_;
  }

private:
  T* value_{};
  bool attempted_{};
};
}  // namespace ug::embedded
