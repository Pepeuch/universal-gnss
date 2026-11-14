// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
#include "ug_service_events.hpp"
#include "ug_service_health.hpp"
namespace ug::embedded
{
void ExpectWorker(ServiceId, std::int64_t deadline_ns);
void ObserveService(ServiceId, ServiceState, std::int32_t error = 0);
ServiceHealthSnapshot ServiceHealths();
ServiceEventSnapshot ServiceEvents();
void ObserveBoot(ServiceId, ServiceState, std::int32_t, void*);
// Each guard belongs to one task. Only that task feeds its own watchdog. No
// watchdog timer/panic reconfiguration and no supervisor-triggered reboot.
class WorkerHealth
{
public:
  WorkerHealth(ServiceId, std::int64_t deadline_ns, bool watchdog);
  ~WorkerHealth();
  WorkerHealth(const WorkerHealth&) = delete;
  WorkerHealth& operator=(const WorkerHealth&) = delete;
  void Progress();

private:
  ServiceId id_;
  std::int64_t last_stack_sample_ns_{};
  bool watchdog_{};
};
}  // namespace ug::embedded
