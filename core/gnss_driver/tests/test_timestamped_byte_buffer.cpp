// SPDX-License-Identifier: LGPL-3.0-only
#include "universal_gnss_driver/detail/timestamped_byte_buffer.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <random>
#include <vector>
namespace
{
unsigned fail_allocation{};
}
void* operator new(std::size_t size)
{
  if (fail_allocation && --fail_allocation == 0)
    throw std::bad_alloc();
  if (auto* p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept
{
  std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
  std::free(p);
}
using universal_gnss_driver::detail::TimestampedByteBuffer;
struct Byte
{
  std::uint8_t value;
  std::optional<std::int64_t> timestamp;
};
void Equal(const TimestampedByteBuffer& actual, const std::vector<Byte>& expected)
{
  assert(actual.size() == expected.size());
  assert(actual.empty() == expected.empty());
  auto cursor = actual.cursor();
  for (std::size_t i = 0; i < expected.size(); ++i)
  {
    assert(cursor.valid() && cursor.value() == expected[i].value &&
           cursor.timestamp() == expected[i].timestamp);
    assert(actual[i] == expected[i].value && actual.data()[i] == expected[i].value);
    cursor.next();
  }
  assert(!cursor.valid());
  for (std::size_t at = 0; at < expected.size(); at += 7)
  {
    auto c = actual.cursor(at);
    assert(c.value() == expected[at].value && c.timestamp() == expected[at].timestamp);
  }
}
int main()
{
  TimestampedByteBuffer buffer;
  std::vector<Byte> reference;
  std::mt19937_64 rng(0x5547425546464552ULL);
  const std::optional<std::int64_t> stamps[] = {
      std::nullopt, 0, -1, 1, INT64_MIN, INT64_MAX, 123456789012345LL};
  for (unsigned step = 0; step < 20000; ++step)
  {
    if (reference.size() > 1024 || rng() % 4 == 0)
    {
      auto count = reference.empty() ? 0 : rng() % (reference.size() + 1);
      buffer.ErasePrefix(count);
      reference.erase(reference.begin(), reference.begin() + count);
    }
    else if (rng() % 20 == 0)
    {
      buffer.clear();
      reference.clear();
    }
    else
    {
      std::vector<std::uint8_t> data(rng() % 65);
      for (auto& byte : data)
        byte = rng();
      auto timestamp = stamps[rng() % 7];
      buffer.Append(data.data(), data.size(), timestamp);
      for (auto value : data)
        reference.push_back({value, timestamp});
    }
    Equal(buffer, reference);
    if (step % 41 == 0)
    {
      auto copy = buffer;
      Equal(copy, reference);
      auto moved = std::move(copy);
      Equal(moved, reference);
      assert(copy.empty());
      copy = std::move(moved);
      assert(moved.empty());
      Equal(copy, reference);
    }
  }
  buffer = TimestampedByteBuffer{};
  std::vector<std::uint8_t> data(4096, 0x5a);
  buffer.Append(data.data(), data.size(), INT64_MIN);
  assert(buffer.retained_bytes() < 5000);  // A single timestamp run, not 4096 optionals.
  buffer = TimestampedByteBuffer{};
  for (std::size_t i = 0; i < 4097; ++i)
    buffer.Append(data.data(), 1, i);
  assert(buffer.retained_bytes() <= 22 * 4097 + 64);  // Worst case: every byte changes timestamp.
  bool rejected = false;
  try
  {
    buffer.Append(data.data(), std::numeric_limits<std::size_t>::max(), 0);
  }
  catch (const std::length_error&)
  {
    rejected = true;
  }
  assert(rejected);
  rejected = false;
  try
  {
    buffer.ErasePrefix(buffer.size() + 1);
  }
  catch (const std::out_of_range&)
  {
    rejected = true;
  }
  assert(rejected);
  buffer.Append(nullptr, 0, std::nullopt);
  auto before = buffer.size();
  rejected = false;
  try
  {
    buffer.Append(nullptr, 1, 0);
  }
  catch (const std::invalid_argument&)
  {
    rejected = true;
  }
  assert(rejected && before == buffer.size());
  for (unsigned failure : {1u, 2u})
  {
    TimestampedByteBuffer failed;
    failed.Append(data.data(), 8, INT64_MAX);
    failed.ErasePrefix(7);
    const std::vector<Byte> expected(1, Byte{data[7], INT64_MAX});
    fail_allocation = failure;
    bool threw = false;
    try
    {
      failed.Append(data.data(), 4096, std::nullopt);
    }
    catch (const std::bad_alloc&)
    {
      threw = true;
    }
    fail_allocation = 0;
    assert(threw);
    Equal(failed, expected);
  }
  TimestampedByteBuffer oversized;
  std::vector<std::uint8_t> large(65536, 0xaa);
  oversized.Append(large.data(), large.size(), 42);
  oversized.ErasePrefix(large.size() - 3);
  std::vector<Byte> tail(3, Byte{0xaa, 42});
  fail_allocation = 1;
  oversized.ConstrainCapacity(16);
  fail_allocation = 0;
  Equal(oversized, tail);
  oversized.ConstrainCapacity(16);
  Equal(oversized, tail);
  assert(oversized.byte_capacity() <= 32);
  oversized.clear();
  oversized.ConstrainCapacity(0);
  assert(oversized.retained_bytes() == 0);
}
