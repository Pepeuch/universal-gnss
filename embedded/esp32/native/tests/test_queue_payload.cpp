// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_gateway.hpp"
#include "ug_queue_payload.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <type_traits>
using namespace ug::embedded;
namespace
{
constexpr std::size_t bytes = 2 * sizeof(Gateway::Frame);
alignas(Gateway::Frame) std::array<std::uint8_t, bytes> arena;
unsigned calls{};
bool refuse{};
void* Allocate(std::size_t size)
{
  ++calls;
  assert(size == arena.size());
  return refuse ? nullptr : arena.data();
}
}  // namespace
int main()
{
  static_assert(!std::is_copy_constructible_v<QueuePayload<bytes>>);
  static_assert(!std::is_copy_assignable_v<QueuePayload<bytes>>);
  QueuePayload<bytes> storage;
  assert(!storage.data() && !storage.Reserve(nullptr) && !calls);
  refuse = true;
  assert(!storage.Reserve(Allocate) && calls == 1 && !storage.data());
  refuse = false;
  arena.fill(0xa5);
  assert(storage.Reserve(Allocate) && calls == 2 && storage.data() == arena.data());
  for (auto value : arena)
    assert(value == 0);
  // Two full frames remain distinct and ordered in the unchanged byte capacity.
  Gateway::Frame first{}, second{}, actual{};
  first.size = 6;
  first.bytes[0] = 0xd3;
  first.receipt_ns = 1234;
  second = first;
  second.bytes[1] = 7;
  second.receipt_ns = 5678;
  std::memcpy(storage.data(), &first, sizeof(first));
  std::memcpy(storage.data() + sizeof(first), &second, sizeof(second));
  refuse = true;
  assert(storage.Reserve(Allocate) && calls == 2);  // Never reallocates a live queue.
  std::memcpy(&actual, storage.data(), sizeof(actual));
  assert(actual.bytes == first.bytes && actual.receipt_ns == first.receipt_ns);
  std::memcpy(&actual, storage.data() + sizeof(first), sizeof(actual));
  assert(actual.bytes == second.bytes && actual.receipt_ns == second.receipt_ns);
}
