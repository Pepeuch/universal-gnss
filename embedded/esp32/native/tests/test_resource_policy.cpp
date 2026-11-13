// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_resource_policy.hpp"
#include <cassert>
#include <initializer_list>
using namespace ug::embedded;
int main()
{
  MemorySample sample{256 * 1024, 128 * 1024, 1, 64 * 1024, 8 * 1024 * 1024, 8 * 1024 * 1024};
  assert(AssessResources(sample) == ResourcePressure::kNormal);
  sample.internal_free = 96 * 1024;
  sample.internal_largest = 32 * 1024;
  assert(AssessResources(sample) == ResourcePressure::kNormal);
  --sample.internal_free;
  assert(AssessResources(sample) == ResourcePressure::kReduced);
  sample.internal_free = 128 * 1024;
  --sample.internal_largest;
  assert(AssessResources(sample) == ResourcePressure::kReduced);
  sample.internal_free = 48 * 1024;
  sample.internal_largest = 16 * 1024;
  assert(AssessResources(sample) == ResourcePressure::kReduced);
  --sample.internal_largest;
  assert(AssessResources(sample) ==
         ResourcePressure::kCritical);  // PSRAM cannot rescue fragmentation.
  sample.internal_largest = 16 * 1024;
  --sample.internal_free;
  assert(AssessResources(sample) == ResourcePressure::kCritical);
  sample.internal_free = 128 * 1024;
  sample.internal_largest = 64 * 1024;
  assert(AssessResources(sample) ==
         ResourcePressure::kNormal);  // Recovery is current, not min-free.
  sample.internal_total = 0;
  assert(AssessResources(sample) == ResourcePressure::kCritical);
  sample.internal_total = 64 * 1024;
  assert(AssessResources(sample) == ResourcePressure::kCritical);
  sample.internal_total = 256 * 1024;
  sample.internal_largest = sample.internal_free + 1;
  assert(AssessResources(sample) == ResourcePressure::kCritical);
  for (const auto pressure :
       {ResourcePressure::kNormal, ResourcePressure::kReduced, ResourcePressure::kCritical})
  {
    assert(AdmitWebWork(pressure, WebWork::kCompactResources));
    assert(AdmitWebWork(pressure, WebWork::kSnapshot) == (pressure != ResourcePressure::kCritical));
    assert(AdmitWebWork(pressure, WebWork::kMutation) == (pressure != ResourcePressure::kCritical));
    assert(AdmitWebWork(pressure, WebWork::kSatelliteReport) ==
           (pressure == ResourcePressure::kNormal));
    assert(!AdmitWebWork(pressure, static_cast<WebWork>(255)));
  }
  assert(!AdmitWebWork(static_cast<ResourcePressure>(255), WebWork::kMutation));
}
