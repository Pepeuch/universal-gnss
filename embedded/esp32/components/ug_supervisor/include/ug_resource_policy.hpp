// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
namespace ug::embedded
{
// One current SDK heap sample. Internal RAM is assessed separately from PSRAM;
// free PSRAM cannot satisfy DMA/task/network allocations requiring internal RAM.
struct MemorySample
{
  std::size_t internal_total{}, internal_free{}, internal_min{}, internal_largest{};
  std::size_t external_total{}, external_free{};
};
enum class ResourcePressure : std::uint8_t
{
  kNormal,
  kReduced,
  kCritical
};
inline const char* ResourcePressureName(ResourcePressure pressure)
{
  switch (pressure)
  {
    case ResourcePressure::kNormal:
      return "normal";
    case ResourcePressure::kReduced:
      return "reduced";
    case ResourcePressure::kCritical:
      return "critical";
  }
  return "invalid";
}
inline ResourcePressure AssessResources(const MemorySample& sample)
{
  // These are conservative admission thresholds, not a reservation or a proof
  // that a future TLS handshake/allocation will succeed. Never use historical
  // minimum-free as the current state: doing so would latch pressure forever.
  if (!sample.internal_total || sample.internal_free > sample.internal_total ||
      sample.internal_largest > sample.internal_free || sample.internal_free < 48 * 1024 ||
      sample.internal_largest < 16 * 1024)
    return ResourcePressure::kCritical;
  if (sample.internal_free < 96 * 1024 || sample.internal_largest < 32 * 1024)
    return ResourcePressure::kReduced;
  return ResourcePressure::kNormal;
}
enum class WebWork : std::uint8_t
{
  kCompactResources,
  kSnapshot,
  kMutation,
  kSatelliteReport
};
inline bool AdmitWebWork(ResourcePressure pressure, WebWork work)
{
  if (work == WebWork::kCompactResources)
    return true;
  if (pressure == ResourcePressure::kCritical ||
      (pressure != ResourcePressure::kNormal && pressure != ResourcePressure::kReduced))
    return false;
  if (work == WebWork::kSatelliteReport)
    return pressure == ResourcePressure::kNormal;
  return work == WebWork::kSnapshot || work == WebWork::kMutation;
}
}  // namespace ug::embedded
