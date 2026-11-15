// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_console_buffer.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
using ug::embedded::ConsoleBuffer;
namespace
{
constexpr std::size_t kSize = 8192 + 32;
unsigned allocated{}, freed{}, wipes{};
bool fail;
void* Allocate(std::size_t size)
{
  assert(size == kSize);
  ++allocated;
  return fail ? nullptr : std::malloc(size);
}
void Wipe(void* data, std::size_t size)
{
  assert(size == kSize);
  ++wipes;
  std::memset(data, 0, size);
}
void Free(void* data)
{
  for (std::size_t i = 0; i < kSize; ++i)
    assert(static_cast<char*>(data)[i] == 0);
  ++freed;
  std::free(data);
}
}  // namespace
int main()
{
  ConsoleBuffer<kSize> line;
  fail = true;
  assert(!line.Initialize(Allocate, Free, Wipe));
  assert(!line.data() && allocated == 1 && !freed);
  fail = false;
  assert(line.Initialize(Allocate, Free, Wipe));
  assert(!line.Initialize(Allocate, Free, Wipe) && allocated == 2);
  for (std::size_t i = 0; i < line.size(); ++i)
    assert(line[i] == 0);
  std::memset(line.data(), 0x5a, line.size());
  line.Reset();  // Startup/task-creation failure uses the same cleanup.
  assert(!line.data() && wipes == 1 && freed == 1);
  line.Reset();
  assert(freed == 1);
  {
    ConsoleBuffer<kSize> scoped;
    assert(scoped.Initialize(Allocate, Free, Wipe));
    scoped[0] = 's';
  }
  assert(freed == 2 && wipes == 2);
}
