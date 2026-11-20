// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_network_allocation.hpp"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "multi_heap.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <new>
#if UG_NETWORK_UNWIND_TRACE
#include <unwind.h>
namespace
{
struct UnwindTrace
{
  std::uintptr_t ip[24]{}, cfa[24]{};
  unsigned count{};
};
UnwindTrace unwind_trace;
std::atomic_flag unwind_trace_busy = ATOMIC_FLAG_INIT;
_Unwind_Reason_Code TraceFrame(_Unwind_Context* context, void* opaque)
{
  auto& trace = *static_cast<UnwindTrace*>(opaque);
  if (trace.count == 24)
    return _URC_END_OF_STACK;
  const auto slot = trace.count++;
  trace.ip[slot] = _Unwind_GetIP(context);
  trace.cfa[slot] = _Unwind_GetCFA(context);
  return _URC_NO_REASON;
}
}  // namespace
extern "C" _Unwind_Reason_Code __real__Unwind_RaiseException(_Unwind_Exception*);
extern "C" _Unwind_Reason_Code __wrap__Unwind_RaiseException(_Unwind_Exception* exception)
{
  const auto reason = __real__Unwind_RaiseException(exception);
  // A successfully caught exception transfers control instead of returning.
  std::printf("UG_MEM unwind_return=%d\n", static_cast<int>(reason));
  if (!unwind_trace_busy.test_and_set())
  {
    unwind_trace.count = 0;
    const auto result = _Unwind_Backtrace(TraceFrame, &unwind_trace);
    std::printf(
        "UG_MEM unwind_trace_result=%d count=%u\n", static_cast<int>(result), unwind_trace.count);
    for (unsigned i = 0; i < unwind_trace.count; ++i)
      std::printf("UG_MEM unwind_frame=%u ip=0x%lx cfa=0x%lx\n",
                  i,
                  static_cast<unsigned long>(unwind_trace.ip[i]),
                  static_cast<unsigned long>(unwind_trace.cfa[i]));
    unwind_trace_busy.clear();
  }
  return reason;
}
#endif
namespace
{
#ifndef UG_NETWORK_ARENA_BYTES
#define UG_NETWORK_ARENA_BYTES (1024 * 1024)
#endif
constexpr std::size_t kArenaBytes = UG_NETWORK_ARENA_BYTES;
static_assert(kArenaBytes >= 65536 && kArenaBytes <= 4 * 1024 * 1024);
struct Header
{
  std::size_t requested;
  unsigned owner;
};
struct Domain
{
  multi_heap_handle_t heap{};
  std::array<ug::embedded::NetworkAllocationStats, 8> counters{};
};
std::atomic<Domain*> domain{};
std::atomic_flag initializing = ATOMIC_FLAG_INIT;
#if UG_NETWORK_ALLOCATION_TEST
std::atomic<unsigned> fail_next_owners{};
#endif
// Controls stay internal; payload, allocation headers and TLSF metadata are PSRAM.
portMUX_TYPE heap_control = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE metrics_control = portMUX_INITIALIZER_UNLOCKED;
class MetricsLock
{
public:
  MetricsLock()
  {
    portENTER_CRITICAL(&metrics_control);
  }
  ~MetricsLock()
  {
    portEXIT_CRITICAL(&metrics_control);
  }
};
void Refused(Domain& data, unsigned owner)
{
  MetricsLock lock;
  ++data.counters[owner].refusals;
}
void* Allocate(std::size_t size, std::size_t alignment)
{
  if (!alignment || (alignment & (alignment - 1)))
    throw std::bad_alloc{};
  if (!size)
    size = 1;
  // Global constructors and IRQ callers never inherit a task allocation domain.
  const bool task = !xPortInIsrContext() && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
  if (task && ug::embedded::network_external_allocation)
  {
    auto* data = domain.load(std::memory_order_acquire);
    if (!data)
      throw std::bad_alloc{};
    const auto owner = ug::embedded::network_allocation_owner;
#if UG_NETWORK_ALLOCATION_TEST
    if (fail_next_owners.fetch_and(~(1u << owner), std::memory_order_relaxed) & (1u << owner))
    {
      Refused(*data, owner);
      throw std::bad_alloc{};
    }
#endif
    if (size > kArenaBytes - sizeof(Header) || alignment > kArenaBytes ||
        size > std::numeric_limits<std::size_t>::max() - sizeof(Header) - (alignment - 1))
    {
      Refused(*data, owner);
      throw std::bad_alloc{};
    }
    auto* raw = multi_heap_aligned_alloc_offs(
        data->heap, sizeof(Header) + size, std::max(alignment, alignof(Header)), sizeof(Header));
    if (!raw)
    {
      Refused(*data, owner);
      throw std::bad_alloc{};
    }
    auto* header = new (raw) Header{size, owner};
    {
      MetricsLock lock;
      auto& count = data->counters[owner];
      ++count.allocations;
      ++count.live;
      count.requested_bytes += size;
      count.peak_requested_bytes = std::max(count.peak_requested_bytes, count.requested_bytes);
    }
    return header + 1;
  }
  for (;;)
  {
    void* result{};
    if (alignment <= alignof(std::max_align_t))
      result = std::malloc(size);
    else if (posix_memalign(&result, alignment, size))
      result = nullptr;
    if (result)
      return result;
    const auto handler = std::get_new_handler();
    if (!handler)
      throw std::bad_alloc{};
    handler();
  }
}
void Release(void* memory) noexcept
{
  if (!memory)
    return;
  auto* data = domain.load(std::memory_order_acquire);
  const auto address = reinterpret_cast<std::uintptr_t>(memory);
  const auto begin = reinterpret_cast<std::uintptr_t>(data);
  if (!data || address < begin || address - begin >= kArenaBytes)
  {
    std::free(memory);
    return;
  }
  // Range ownership is constant-time and disjoint from the ordinary SDK heap.
  // Scoped payloads are task/cache-on only, never transferred to an ISR.
  if (xPortInIsrContext())
    std::abort();
  auto* header = static_cast<Header*>(memory) - 1;
  const auto requested = header->requested;
  const auto owner = header->owner;
  if (owner >= data->counters.size() || requested > kArenaBytes ||
      address - begin > kArenaBytes - requested)
    std::abort();
  auto* bytes = static_cast<volatile unsigned char*>(memory);
  for (std::size_t i = 0; i < requested; ++i)
    bytes[i] = 0;
  {
    MetricsLock lock;
    auto& count = data->counters[owner];
    --count.live;
    count.requested_bytes -= requested;
  }
  // The SDK heap owns its recursive internal lock. Never heap_caps_free an
  // interior arena block, and never wipe allocator padding/poison canaries.
  multi_heap_free(data->heap, header);
}
}  // namespace
namespace ug::embedded
{
#if UG_NETWORK_ALLOCATION_TEST
void FailNextNetworkAllocationForTest(unsigned owner)
{
  if (owner < 8)
    fail_next_owners.fetch_or(1u << owner, std::memory_order_relaxed);
}
#endif
bool InitializeNetworkAllocation()
{
  if (domain.load(std::memory_order_acquire))
    return true;
  if (initializing.test_and_set(std::memory_order_acquire))
    return false;
  if (domain.load(std::memory_order_acquire))
  {
    initializing.clear(std::memory_order_release);
    return true;
  }
  void* raw = heap_caps_malloc(kArenaBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!raw)
  {
    initializing.clear(std::memory_order_release);
    return false;
  }
  auto* data = new (raw) Domain{};
  constexpr auto prefix =
      (sizeof(Domain) + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1);
  data->heap = multi_heap_register(static_cast<unsigned char*>(raw) + prefix, kArenaBytes - prefix);
  if (!data->heap)
  {
    data->~Domain();
    heap_caps_free(raw);
    initializing.clear(std::memory_order_release);
    return false;
  }
  multi_heap_set_lock(data->heap, &heap_control);
#if UG_NETWORK_ALLOCATION_TEST
  std::set_terminate([] {
    std::printf("UG_MEM cpp_terminate owner=%u scoped=%d uncaught=%d\n",
                network_allocation_owner,
                network_external_allocation,
                std::uncaught_exceptions());
    std::abort();
  });
#endif
  domain.store(data, std::memory_order_release);
  initializing.clear(std::memory_order_release);
  return true;
}
std::array<NetworkAllocationStats, 8> NetworkCppAllocations()
{
  auto* data = domain.load(std::memory_order_acquire);
  if (!data)
    return {};
  MetricsLock lock;
  return data->counters;
}
NetworkArenaStats NetworkArenaMemory()
{
  auto* data = domain.load(std::memory_order_acquire);
  if (!data)
    return {};
  multi_heap_info_t info{};
  multi_heap_get_info(data->heap, &info);
  return {kArenaBytes, info.total_free_bytes, info.minimum_free_bytes, info.largest_free_block};
}
#if UG_NETWORK_ALLOCATION_TEST
bool CheckNetworkArenaIntegrity()
{
  auto* data = domain.load(std::memory_order_acquire);
  return data && multi_heap_check(data->heap, true);
}
#endif
}  // namespace ug::embedded
void* operator new(std::size_t n)
{
  return Allocate(n, alignof(std::max_align_t));
}
void* operator new[](std::size_t n)
{
  return Allocate(n, alignof(std::max_align_t));
}
void* operator new(std::size_t n, std::align_val_t a)
{
  return Allocate(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a)
{
  return Allocate(n, static_cast<std::size_t>(a));
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
  try
  {
    return ::operator new(n);
  }
  catch (...)
  {
    return nullptr;
  }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept
{
  try
  {
    return ::operator new[](n);
  }
  catch (...)
  {
    return nullptr;
  }
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
  try
  {
    return ::operator new(n, a);
  }
  catch (...)
  {
    return nullptr;
  }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
  try
  {
    return ::operator new[](n, a);
  }
  catch (...)
  {
    return nullptr;
  }
}
void operator delete(void* p) noexcept
{
  Release(p);
}
void operator delete[](void* p) noexcept
{
  Release(p);
}
void operator delete(void* p, std::size_t) noexcept
{
  Release(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
  Release(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
  Release(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
  Release(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
  Release(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
  Release(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept
{
  Release(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
  Release(p);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept
{
  Release(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept
{
  Release(p);
}
