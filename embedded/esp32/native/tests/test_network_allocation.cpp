// SPDX-License-Identifier: LGPL-3.0-only
#include "../../components/ug_network/src/ug_network_allocation.cpp"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "ug_network_allocation.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <vector>
namespace
{
struct Block
{
  void* p;
  std::size_t size;
  bool external;
  bool arena{};
  Block* next;
  void* wiped{};
  std::size_t wipe_size{};
};
Block* blocks{};
std::mutex guard;
thread_local bool refuse{};
thread_local bool isr{};
thread_local unsigned critical_depth{};
unsigned cleared{};
Block* Find(const void* p)
{
  for (auto* b = blocks; b; b = b->next)
    if (reinterpret_cast<std::uintptr_t>(p) >= reinterpret_cast<std::uintptr_t>(b->p) &&
        reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(b->p) < b->size)
      return b;
  return nullptr;
}
void WatchWipe(void* p, std::size_t n)
{
  std::lock_guard<std::mutex> lock(guard);
  auto* b = Find(p);
  assert(b);
  b->wiped = p;
  b->wipe_size = n;
}
}  // namespace
bool xPortInIsrContext()
{
  return isr;
}
void AllocationCriticalEnter(portMUX_TYPE* control)
{
  control->mutex.lock();
  assert(critical_depth++ == 0);
}
void AllocationCriticalExit(portMUX_TYPE* control)
{
  assert(--critical_depth == 0);
  control->mutex.unlock();
}
int xTaskGetSchedulerState()
{
  return 2;
}
void* heap_caps_aligned_alloc(std::size_t alignment, std::size_t size, unsigned caps)
{
  assert(critical_depth == 0);
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) || caps == MALLOC_CAP_DEFAULT);
  if (refuse)
    return nullptr;
  void* p{};
  if (posix_memalign(&p, alignment, size + 16))
    return nullptr;
  std::memset(static_cast<unsigned char*>(p) + size, 0xa7, 16);
  auto* b = static_cast<Block*>(std::malloc(sizeof(Block)));
  assert(b);
  std::lock_guard<std::mutex> lock(guard);
  *b = {p, size, caps != MALLOC_CAP_DEFAULT, false, blocks};
  blocks = b;
  return p;
}
void* heap_caps_malloc(std::size_t n, unsigned caps)
{
  return heap_caps_aligned_alloc(alignof(std::max_align_t), n, caps);
}
bool esp_ptr_external_ram(const void* p)
{
  std::lock_guard<std::mutex> lock(guard);
  auto* b = Find(p);
  return b && b->external;
}
std::size_t heap_caps_get_allocated_size(void* p)
{
  std::lock_guard<std::mutex> lock(guard);
  auto* b = Find(p);
  assert(b);
  return b->size;
}
void Remove(void* p, bool arena)
{
  std::lock_guard<std::mutex> lock(guard);
  for (auto** slot = &blocks; *slot; slot = &(*slot)->next)
    if ((*slot)->p == p)
    {
      auto* b = *slot;
      assert(b->arena == arena);
      for (std::size_t i = 0; i < 16; ++i)
        assert(static_cast<unsigned char*>(p)[b->size + i] == 0xa7);
      if (b->wiped)
      {
        for (std::size_t i = 0; i < b->wipe_size; ++i)
          assert(static_cast<unsigned char*>(b->wiped)[i] == 0);
        ++cleared;
      }
      *slot = b->next;
      std::free(b);
      return;
    }
  assert(false);
}
void heap_caps_free(void* p)
{
  assert(critical_depth == 0);
  Remove(p, false);
  std::free(p);
}
// A bounded address-range provider seam, not a substitute for SDK TLSF. Actual
// coalescing, alignment, canaries and cross-core locks run in the SDK/QEMU suite.
struct MockHeap
{
  unsigned char* begin{};
  std::size_t size{}, used{}, live{}, minimum{};
  portMUX_TYPE* lock{};
};
MockHeap arena_heap;
multi_heap_handle_t multi_heap_register(void* start, std::size_t size)
{
  arena_heap = {static_cast<unsigned char*>(start), size, 0, 0, size, nullptr};
  return &arena_heap;
}
void multi_heap_set_lock(multi_heap_handle_t h, void* lock)
{
  h->lock = static_cast<portMUX_TYPE*>(lock);
}
void* multi_heap_aligned_alloc_offs(multi_heap_handle_t h,
                                    std::size_t size,
                                    std::size_t align,
                                    std::size_t offset)
{
  if (refuse)
    return nullptr;
  AllocationCriticalEnter(h->lock);
  const auto address =
      (reinterpret_cast<std::uintptr_t>(h->begin + h->used) + offset + align - 1) & ~(align - 1);
  auto* raw = reinterpret_cast<unsigned char*>(address - offset);
  const auto used = static_cast<std::size_t>(raw - h->begin) + size + 16;
  if (used > h->size)
  {
    AllocationCriticalExit(h->lock);
    return nullptr;
  }
  h->used = used;
  ++h->live;
  h->minimum = std::min(h->minimum, h->size - h->used);
  auto* b = static_cast<Block*>(std::malloc(sizeof(Block)));
  assert(b);
  std::memset(raw + size, 0xa7, 16);
  {
    std::lock_guard<std::mutex> lock(guard);
    *b = {raw, size, true, true, blocks};
    blocks = b;
  }
  AllocationCriticalExit(h->lock);
  return raw;
}
void multi_heap_free(multi_heap_handle_t h, void* p)
{
  AllocationCriticalEnter(h->lock);
  Remove(p, true);
  assert(h->live);
  if (!--h->live)
    h->used = 0;
  AllocationCriticalExit(h->lock);
}
void multi_heap_get_info(multi_heap_handle_t h, multi_heap_info_t* info)
{
  AllocationCriticalEnter(h->lock);
  *info = {h->size - h->used, h->used, h->size - h->used, h->minimum};
  AllocationCriticalExit(h->lock);
}
int main()
{
  using namespace ug::embedded;
  auto* early_normal = new char[37];
  refuse = true;  // delete before registry initialization must not allocate.
  delete[] early_normal;
  refuse = false;
  {
    NetworkAllocationScope early;
    bool failed = false;
    try
    {
      (void)::operator new(33);
    }
    catch (const std::bad_alloc&)
    {
      failed = true;
    }
    assert(failed);  // No silent default-heap fallback before domain initialization.
  }
  refuse = true;
  assert(!InitializeNetworkAllocation());
  refuse = false;
  assert(InitializeNetworkAllocation() && InitializeNetworkAllocation());
  auto* normal = new char[256];
  assert(!esp_ptr_external_ram(normal));
  delete[] normal;
  std::vector<char> payload;
  {
    NetworkAllocationScope scope;
    payload.resize(513, 's');
    assert(esp_ptr_external_ram(payload.data()));
    WatchWipe(payload.data(), payload.capacity());
    {
      NetworkAllocationScope nested;
      struct alignas(64) Aligned
      {
        char secret[96];
      };
      auto* value = new (std::nothrow) Aligned;
      assert(value && reinterpret_cast<std::uintptr_t>(value) % 64 == 0);
      assert(esp_ptr_external_ram(value));
      WatchWipe(value, sizeof(*value));
      std::memset(value, 0x5a, sizeof(*value));
      delete value;
    }
    assert(network_external_allocation);
    isr = true;
    auto* internal = new char[16];
    assert(!esp_ptr_external_ram(internal));
    delete[] internal;
    isr = false;
    refuse = true;
    assert(new (std::nothrow) char[17] == nullptr);
    bool failed = false;
    try
    {
      payload.resize(4096);
    }
    catch (const std::bad_alloc&)
    {
      failed = true;
    }
    assert(failed && payload.size() == 513 && payload[0] == 's');
    refuse = false;
    std::thread ordinary([] {
      auto* p = new char[42];
      assert(!esp_ptr_external_ram(p));
      delete[] p;
    });
    ordinary.join();
  }
  assert(!network_external_allocation);
  payload = std::vector<char>{};  // Free outside the scope must still erase secrets.
  struct ConstructorFails
  {
    char secret[73];
    ConstructorFails()
    {
      WatchWipe(this, sizeof(*this));
      std::memset(secret, 0x5a, sizeof(secret));
      throw std::bad_alloc{};
    }
  };
  try
  {
    NetworkAllocationScope scope;
    (void)new ConstructorFails;
    assert(false);
  }
  catch (const std::bad_alloc&)
  {
  }
  assert(!network_external_allocation && blocks && !blocks->next && cleared >= 3);
  const auto stats = NetworkCppAllocations();
  assert(stats[0].live == 0 && stats[0].requested_bytes == 0 && stats[0].refusals >= 2);
  void* transferred{};
  {
    NetworkAllocationScope payload_owner(3);
    transferred = ::operator new(65);
    std::memset(transferred, 0x5a, 65);
    WatchWipe(transferred, 65);
  }
  std::thread other([&] { ::operator delete(transferred); });
  other.join();
  assert(NetworkCppAllocations()[3].live == 0 && NetworkCppAllocations()[3].requested_bytes == 0);
  for (std::size_t n = 1; n <= 65; ++n)
  {
    NetworkAllocationScope payload_owner;
    auto* value = ::operator new(n, std::align_val_t{64});
    assert(reinterpret_cast<std::uintptr_t>(value) % 64 == 0);
    std::memset(value, 0x5a, n);
    WatchWipe(value, n);
    ::operator delete(value, std::align_val_t{64});
  }
  // Existing monitor retention may legitimately exceed 4096 C++ nodes. Arena
  // admission depends on bytes, not arbitrary hash collisions or block count.
  std::array<void*, 6000> held{};
  {
    NetworkAllocationScope payload_owner;
    for (auto& p : held)
      p = ::operator new(16);
  }
  assert(NetworkCppAllocations()[0].live == held.size());
  for (auto* p : held)
    ::operator delete(p);
  assert(NetworkCppAllocations()[0].live == 0);
  const auto arena = NetworkArenaMemory();
  assert(arena.reserved == 1024 * 1024 && arena.free > 900000);
  {
    NetworkAllocationScope payload_owner;
    assert(::operator new(arena.reserved, std::nothrow) == nullptr);
    assert(::operator new(SIZE_MAX, std::nothrow) == nullptr);
    assert(::operator new(SIZE_MAX - 64, std::nothrow) == nullptr);
    assert(::operator new(16, std::align_val_t{1u << 30}, std::nothrow) == nullptr);
  }
}
