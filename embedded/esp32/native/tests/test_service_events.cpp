// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_service_events.hpp"
#include <cassert>
#include <type_traits>
using namespace ug::embedded;
int main()
{
  static_assert(std::is_trivially_copyable_v<ServiceEvent>);
  ServiceEventRing ring;
  assert(ring.Snapshot().count == 0);
  assert(!ring.Append(
      static_cast<ServiceId>(255), ServiceState::kRunning, ServiceEventKind::kState, 0, 0));
  assert(!ring.Append(
      ServiceId::kRouting, static_cast<ServiceState>(255), ServiceEventKind::kState, 0, 0));
  assert(!ring.Append(
      ServiceId::kRouting, ServiceState::kRunning, static_cast<ServiceEventKind>(255), 0, 0));
  assert(
      !ring.Append(ServiceId::kRouting, ServiceState::kRunning, ServiceEventKind::kState, 0, -1));
  for (std::size_t i = 0; i < kServiceEventCapacity + 3; ++i)
    assert(ring.Append(ServiceId::kRouting,
                       ServiceState::kFailed,
                       ServiceEventKind::kState,
                       -static_cast<int>(i),
                       i * 100));
  const auto copy = ring.Snapshot();
  assert(copy.count == kServiceEventCapacity && copy.overwritten == 3 && copy.rejected == 4);
  for (std::size_t i = 0; i < copy.count; ++i)
  {
    const auto& event = copy.events[i];
    assert(event.sequence == i + 4 && event.receipt_ns == static_cast<std::int64_t>((i + 3) * 100));
    assert(event.error == -static_cast<int>(i + 3));
  }
  assert(ring.Snapshot().events[0].sequence == 4);  // Non-destructive multiple consumers.
  assert(ring.Append(ServiceId::kGnss, ServiceState::kFailed, ServiceEventKind::kWorkerExit, 7, 1));
  assert(copy.events[0].sequence == 4 && ring.Snapshot().events[0].sequence == 5);
  assert(ring.Snapshot().events.back().receipt_ns ==
         1);  // Clock rollback retained, never freshened.
}
