// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
#include <new>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifndef UG_S3_SATELLITE_PSRAM
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
#define UG_S3_SATELLITE_PSRAM 1
#else
#define UG_S3_SATELLITE_PSRAM 0
#endif
#endif
namespace ug::embedded
{
// SDK boundary; no fallback to internal memory. Called after platform startup.
void* AllocateSatelliteMemory(std::size_t size);
void FreeSatelliteMemory(void* data);
// Single-owner, stable-address storage. Never copied into snapshots or queues.
// Construction performs no allocation: native startup must initialize before
// creating the GNSS owner. Host Observe also permits first-use initialization.
template <class T>
class SatelliteStorage
{
public:
  SatelliteStorage() = default;
  SatelliteStorage(const SatelliteStorage&) = delete;
  SatelliteStorage& operator=(const SatelliteStorage&) = delete;
#if UG_S3_SATELLITE_PSRAM
  ~SatelliteStorage()
  {
    if (value_)
    {
      value_->~T();
      FreeSatelliteMemory(value_);
    }
  }
  bool Initialize()
  {
    if (attempted_)
      return value_ != nullptr;
    attempted_ = true;
    auto* memory = AllocateSatelliteMemory(sizeof(T));
    if (memory)
      value_ = new (memory) T{};
    return value_ != nullptr;
  }
  T* Get()
  {
    return value_;
  }
  const T* Get() const
  {
    return value_;
  }

private:
  T* value_{};
  bool attempted_{};
#else
  bool Initialize()
  {
    return true;
  }
  T* Get()
  {
    return &value_;
  }
  const T* Get() const
  {
    return &value_;
  }

private:
  T value_{};
#endif
};
}  // namespace ug::embedded
