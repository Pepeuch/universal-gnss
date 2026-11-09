#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace universal_gnss_transport
{

template <std::size_t Capacity>
class ByteRingBuffer
{
public:
  static_assert(Capacity > 0u, "ByteRingBuffer capacity must be greater than zero");

  constexpr std::size_t capacity() const
  {
    return Capacity;
  }

  std::size_t size() const
  {
    return size_;
  }

  bool empty() const
  {
    return size_ == 0u;
  }

  bool full() const
  {
    return size_ == Capacity;
  }

  std::size_t overflow_count() const
  {
    return overflow_count_;
  }

  bool Push(const std::uint8_t byte)
  {
    if (full())
    {
      ++overflow_count_;
      return false;
    }

    storage_[tail_] = byte;
    tail_ = (tail_ + 1u) % Capacity;
    ++size_;
    return true;
  }

  std::size_t Push(const std::uint8_t* data, const std::size_t count)
  {
    if (data == nullptr && count != 0u)
    {
      return 0u;
    }

    std::size_t pushed = 0u;
    for (std::size_t index = 0u; index < count; ++index)
    {
      if (!Push(data[index]))
      {
        continue;
      }
      ++pushed;
    }

    return pushed;
  }

  std::optional<std::uint8_t> Pop()
  {
    if (empty())
    {
      return std::nullopt;
    }

    const std::uint8_t value = storage_[head_];
    head_ = (head_ + 1u) % Capacity;
    --size_;
    return value;
  }

  std::size_t Pop(std::uint8_t* destination, const std::size_t max_count)
  {
    if (destination == nullptr && max_count != 0u)
    {
      return 0u;
    }

    std::size_t popped = 0u;
    while (popped < max_count)
    {
      const auto value = Pop();
      if (!value.has_value())
      {
        break;
      }
      destination[popped] = *value;
      ++popped;
    }

    return popped;
  }

  void Clear()
  {
    head_ = 0u;
    tail_ = 0u;
    size_ = 0u;
    overflow_count_ = 0u;
  }

private:
  std::array<std::uint8_t, Capacity> storage_{};
  std::size_t head_{0u};
  std::size_t tail_{0u};
  std::size_t size_{0u};
  std::size_t overflow_count_{0u};
};

}  // namespace universal_gnss_transport
