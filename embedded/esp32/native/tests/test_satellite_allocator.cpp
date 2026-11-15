// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_satellite_storage.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
namespace ug::embedded
{
namespace
{
std::unordered_map<void*, std::size_t> live;
}
void* AllocateSatelliteMemory(std::size_t size)
{
  auto* p = std::malloc(size + 16);
  assert(p);
  std::memset(static_cast<char*>(p) + size, 0x5a, 16);
  live.emplace(p, size);
  return p;
}
void FreeSatelliteMemory(void* p)
{
  const auto size = live.at(p);
  for (std::size_t i = size; i < size + 16; ++i)
    assert(static_cast<unsigned char*>(p)[i] == 0x5a);
  live.erase(p);
  std::free(p);
}
}  // namespace ug::embedded
