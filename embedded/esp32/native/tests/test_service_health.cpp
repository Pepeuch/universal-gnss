// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_supervisor.hpp"
#include <cassert>
using namespace ug::embedded;
std::int64_t now_us = 1;
int added = 0, fed = 0, removed = 0, add_error = 0, reset_error = 0;
std::uint32_t stack_bytes = 768;
std::int64_t esp_timer_get_time()
{
  return now_us;
}
int esp_task_wdt_add(void* task)
{
  assert(!task);
  ++added;
  return add_error;
}
int esp_task_wdt_delete(void* task)
{
  assert(!task);
  ++removed;
  return 0;
}
int esp_task_wdt_reset()
{
  ++fed;
  return reset_error;
}
std::uint32_t uxTaskGetStackHighWaterMark(void*)
{
  return stack_bytes;
}
static std::size_t Index(ServiceId id)
{
  return static_cast<std::size_t>(id);
}
static std::int32_t Start(void* context)
{
  ++*static_cast<int*>(context);
  return 0;
}
static std::int32_t Fail(void* context)
{
  ++*static_cast<int*>(context);
  return 123;
}
int main()
{
  int core = 0, network = 0, gnss = 0, console = 0;
  const BootStep steps[] = {
      {ServiceId::kNetworkCore, 0, Fail, &core},
      {ServiceId::kNtripSetup, ServiceBit(ServiceId::kNetworkCore), Start, &network},
      {ServiceId::kGnss, 0, Start, &gnss},
      {ServiceId::kConsole, 0, Start, &console}};
  const auto result = RunBoot(steps, 4, ObserveBoot);
  assert(result.plan_valid && core == 1 && network == 0 && gnss == 1 && console == 1);
  assert(result.failed == ServiceBit(ServiceId::kNetworkCore));
  assert(result.blocked == ServiceBit(ServiceId::kNtripSetup));
  assert(ServicesDegraded(ServiceHealths(), now_us * 1000));
  const auto boot_events = ServiceEvents();
  assert(boot_events.count == 7);
  assert(boot_events.events[1].service == ServiceId::kNetworkCore &&
         boot_events.events[1].state == ServiceState::kFailed &&
         boot_events.events[1].error == 123);
  assert(boot_events.events[2].service == ServiceId::kNtripSetup &&
         boot_events.events[2].state == ServiceState::kBlocked);
  auto bad = steps[0];
  bad.id = static_cast<ServiceId>(255);
  assert(!RunBoot(&bad, 1, ObserveBoot).plan_valid);
  const BootStep duplicates[] = {steps[0], steps[0]};
  assert(!RunBoot(duplicates, 2, ObserveBoot).plan_valid && core == 1);  // No partial side effects.
  const BootStep ordered[] = {
      {ServiceId::kNetworkCore, 0, Start, &core},
      {ServiceId::kNtripSetup, ServiceBit(ServiceId::kNetworkCore), Start, &network}};
  const auto success = RunBoot(ordered, 2, ObserveBoot);
  assert(success.plan_valid && !success.failed && !success.blocked && core == 2 && network == 1);
  ExpectWorker(ServiceId::kRouting, 5000000000LL);
  ObserveService(ServiceId::kRouting, ServiceState::kRunning);
  auto state = ServiceHealths()[Index(ServiceId::kRouting)];
  assert(state.state == ServiceState::kStarting && !state.entered);
  assert(!ServiceStale(state, 5000001000LL) && ServiceStale(state, 5000001001LL));
  assert(ServiceStale(state, 0));  // Clock rollback cannot report healthy progress.
  state.deadline_ns = 0;
  assert(ServiceStale(state, 1000));
  state.progress_ns = INT64_MIN;
  state.deadline_ns = 1;
  assert(ServiceStale(state, INT64_MAX));
  const BootStep disabled[] = {
      {ServiceId::kRadio, ServiceBit(ServiceId::kNetworkCore), Fail, &core, false}};
  assert(RunBoot(disabled, 1, ObserveBoot).plan_valid && core == 2);
  assert(ServiceHealths()[Index(ServiceId::kRadio)].state == ServiceState::kDisabled);
  {
    WorkerHealth router(ServiceId::kRouting, 5000000000LL, true);
    assert(added == 1 && fed == 0);
    ObserveService(ServiceId::kRouting,
                   ServiceState::kStarting);  // Startup race, task entered first.
    ObserveService(ServiceId::kRouting, ServiceState::kRunning);
    now_us = 1000001;
    router.Progress();
    state = ServiceHealths()[Index(ServiceId::kRouting)];
    assert(state.entered && state.watchdog_registered && state.stack_sampled &&
           state.stack_min_bytes == 768);
    assert(state.progress_ns == 1000001000LL && fed == 1);
    // Reading status never feeds a worker, even when its progress is stale.
    (void)ServicesDegraded(ServiceHealths(), 9000000000LL);
    assert(fed == 1);
    stack_bytes = 0;
    now_us = 2000001;
    reset_error = 7;
    router.Progress();
    const auto fault_events = ServiceEvents();
    assert(fault_events.events[fault_events.count - 1].kind == ServiceEventKind::kWatchdog &&
           fault_events.events[fault_events.count - 1].error == 7);
    router.Progress();
    assert(ServiceEvents().count == fault_events.count);  // Repeated faults cannot flood log.
    state = ServiceHealths()[Index(ServiceId::kRouting)];
    assert(state.stack_sampled && state.stack_min_bytes == 0 && state.watchdog_error == 7);
  }
  assert(!ServiceHealths()[Index(ServiceId::kRouting)].watchdog_registered);
  assert(removed == 1 &&
         ServiceHealths()[Index(ServiceId::kRouting)].state == ServiceState::kFailed);
  {
    WorkerHealth network_worker(ServiceId::kNtripInput, 30000000000LL, false);
    network_worker.Progress();
    assert(added == 1 && fed == 3);
  }
  add_error = 9;
  {
    WorkerHealth unavailable(ServiceId::kGnss, 5000000000LL, true);
    unavailable.Progress();
    state = ServiceHealths()[Index(ServiceId::kGnss)];
    assert(!state.watchdog_registered && state.watchdog_error == 9 && fed == 3);
  }
  assert(removed == 1);
}
