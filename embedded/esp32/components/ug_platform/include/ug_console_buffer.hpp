// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstring>
namespace ug::embedded
{
// Explicit allocator boundary: failure never falls back to another memory class.
// Single console owner; zeroize before release, including task-creation failure.
template <std::size_t Capacity>
class ConsoleBuffer
{
public:
  using Allocate = void* (*)(std::size_t);
  using Release = void (*)(void*);
  using Wipe = void (*)(void*, std::size_t);
  ~ConsoleBuffer()
  {
    Reset();
  }
  ConsoleBuffer() = default;
  ConsoleBuffer(const ConsoleBuffer&) = delete;
  ConsoleBuffer& operator=(const ConsoleBuffer&) = delete;
  bool Initialize(Allocate allocate, Release release, Wipe wipe)
  {
    if (data_ || !allocate || !release || !wipe)
      return false;
    auto* next = static_cast<char*>(allocate(Capacity));
    if (!next)
      return false;
    std::memset(next, 0, Capacity);
    release_ = release;
    wipe_ = wipe;
    data_ = next;
    return true;
  }
  void Reset()
  {
    if (data_)
    {
      wipe_(data_, Capacity);
      release_(data_);
      data_ = nullptr;
    }
  }
  char* data() const
  {
    return data_;
  }
  constexpr std::size_t size() const
  {
    return Capacity;
  }
  char& operator[](std::size_t index)
  {
    return data_[index];
  }

private:
  char* data_{};
  Release release_{};
  Wipe wipe_{};
};
}  // namespace ug::embedded
