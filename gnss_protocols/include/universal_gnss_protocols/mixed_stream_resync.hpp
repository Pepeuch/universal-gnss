#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "universal_gnss_protocols/unicore_binary_framer.hpp"

namespace universal_gnss_protocols
{

inline bool IsMixedTextSyncByte(const std::uint8_t byte)
{
  return byte == '$' || byte == '!' || byte == '#' || byte == '%';
}

template <typename ByteAccessor>
bool HasUnicoreBinarySyncAt(const std::size_t size,
                            ByteAccessor&& get_byte,
                            const std::size_t offset)
{
  return offset + 2u < size && get_byte(offset) == kUnicoreBinarySync1 &&
         get_byte(offset + 1u) == kUnicoreBinarySync2 &&
         get_byte(offset + 2u) == kUnicoreBinarySync3;
}

template <typename ByteAccessor>
bool IsPlausibleMixedTextRecordStart(const std::size_t size,
                                     ByteAccessor&& get_byte,
                                     const std::size_t offset)
{
  if (offset >= size || !IsMixedTextSyncByte(get_byte(offset)) || offset + 1u >= size)
  {
    return false;
  }

  const std::size_t max_probe = std::min<std::size_t>(size, offset + 20u);
  bool saw_separator = false;
  for (std::size_t index = offset + 1u; index < max_probe; ++index)
  {
    const char c = static_cast<char>(get_byte(index));
    if (c == ',' || c == ';' || c == '*' || c == ' ')
    {
      saw_separator = true;
      break;
    }

    const bool valid = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (!valid)
    {
      return false;
    }
  }

  return saw_separator;
}

template <typename ByteAccessor>
std::optional<std::size_t> FindEmbeddedMixedRecordResyncOffset(const std::size_t size,
                                                               ByteAccessor&& get_byte,
                                                               const std::size_t start_offset)
{
  for (std::size_t offset = start_offset + 1u; offset < size; ++offset)
  {
    if (get_byte(offset) == '\n')
    {
      return std::nullopt;
    }

    if (HasUnicoreBinarySyncAt(size, get_byte, offset) ||
        IsPlausibleMixedTextRecordStart(size, get_byte, offset))
    {
      return offset;
    }
  }

  return std::nullopt;
}

}  // namespace universal_gnss_protocols
