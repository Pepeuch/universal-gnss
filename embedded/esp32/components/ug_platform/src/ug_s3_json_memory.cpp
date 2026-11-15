// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_s3_json_memory.hpp"
#include "sdkconfig.h"
#include "ug_s3_memory_options.hpp"
#if CONFIG_IDF_TARGET_ESP32S3
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "mbedtls/platform_util.h"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
namespace ug::embedded
{
namespace
{
std::atomic<int> fault_budget{-1};
std::atomic<std::uint32_t> failures{}, injected{};
#if UG_S3_JSON_PSRAM
// Preserve the allocator's alignment without dereferencing an over-aligned
// header. Store the requested size: heap usable-size may include poison padding.
constexpr std::size_t kPrefix = (sizeof(std::size_t) + alignof(std::max_align_t) - 1) /
                                alignof(std::max_align_t) * alignof(std::max_align_t);
void* JsonMemoryAllocate(std::size_t size)
{
  auto budget = fault_budget.load();
  while (budget >= 0)
  {
    const int next = budget ? budget - 1 : -1;
    if (fault_budget.compare_exchange_weak(budget, next))
    {
      if (!budget)
      {
        ++failures;
        ++injected;
        return nullptr;
      }
      break;
    }
  }
  if (size > std::numeric_limits<std::size_t>::max() - kPrefix)
  {
    ++failures;
    return nullptr;
  }
  auto* base = static_cast<unsigned char*>(
      heap_caps_malloc(kPrefix + size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!base)
  {
    ++failures;
    return nullptr;
  }
  std::memcpy(base, &size, sizeof(size));
  return base + kPrefix;
}
void JsonMemoryFree(void* data)
{
  if (!data)
    return;
  auto* base = static_cast<unsigned char*>(data) - kPrefix;
  std::size_t requested{};
  std::memcpy(&requested, base, sizeof(requested));
  // Wipe precisely the owned request, including metadata, without touching
  // allocator guards/padding. JSON can contain provisioned credentials.
  mbedtls_platform_zeroize(base, kPrefix + requested);
  heap_caps_free(base);
}

#endif
}  // namespace
std::uint32_t S3JsonMemoryFailures()
{
  return failures.load();
}
std::uint32_t S3JsonInjectedFailures()
{
  return injected.load();
}
bool ArmS3JsonMemoryFailure(int budget)
{
#if UG_S3_JSON_PSRAM
  if (budget < -1 || budget > 4096)
    return false;
  fault_budget.store(budget);
  return true;
#else
  (void)budget;
  return false;
#endif
}
void InitializeS3JsonMemory()
{
#if UG_S3_JSON_PSRAM
  cJSON_Hooks hooks{JsonMemoryAllocate, JsonMemoryFree};
  cJSON_InitHooks(&hooks);
#endif
}
}  // namespace ug::embedded
#else
namespace ug::embedded
{
void InitializeS3JsonMemory()
{
}
std::uint32_t S3JsonMemoryFailures()
{
  return 0;
}
std::uint32_t S3JsonInjectedFailures()
{
  return 0;
}
bool ArmS3JsonMemoryFailure(int)
{
  return false;
}
}  // namespace ug::embedded
#endif
