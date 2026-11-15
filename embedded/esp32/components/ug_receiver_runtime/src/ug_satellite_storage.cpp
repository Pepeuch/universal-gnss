// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_satellite_storage.hpp"
#if defined(ESP_PLATFORM) && UG_S3_SATELLITE_PSRAM
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include <cstdio>
#endif
namespace ug::embedded
{
void* AllocateSatelliteMemory(std::size_t size)
{
#if defined(ESP_PLATFORM) && UG_S3_SATELLITE_PSRAM
  auto* memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  std::printf("UG_MEM satellite_storage bytes=%zu external=%d ready=%d\n",
              size,
              memory ? esp_ptr_external_ram(memory) : 0,
              memory != nullptr);
  return memory;
#else
  (void)size;
  return nullptr;
#endif
}
void FreeSatelliteMemory(void* data)
{
#if defined(ESP_PLATFORM) && UG_S3_SATELLITE_PSRAM
  heap_caps_free(data);
#else
  (void)data;
#endif
}
}  // namespace ug::embedded
