// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_supervisor.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <mutex>
namespace ug::embedded
{
namespace
{
std::mutex guard;
ServiceHealthSnapshot services;
ServiceEventRing events;
std::int64_t Now()
{
  return esp_timer_get_time() * 1000;
}
}  // namespace
void ExpectWorker(ServiceId id, std::int64_t deadline_ns)
{
  if (!ValidService(id))
    return;
  std::lock_guard<std::mutex> lock(guard);
  auto& service = services[static_cast<std::size_t>(id)];
  service = {};
  service.state = ServiceState::kStarting;
  service.worker = true;
  service.deadline_ns = deadline_ns;
  service.progress_ns = Now();
  events.Append(id, service.state, ServiceEventKind::kState, 0, service.progress_ns);
}
void ObserveService(ServiceId id, ServiceState state, std::int32_t error)
{
  if (!ValidService(id))
    return;
  std::lock_guard<std::mutex> lock(guard);
  auto& service = services[static_cast<std::size_t>(id)];
  // A task may already have entered/progressed before xTaskCreate returns.
  // Startup bookkeeping must not erase its watchdog/stack/progress evidence.
  if (state == ServiceState::kStarting && service.state == ServiceState::kRunning)
    return;
  const auto before_state = service.state;
  const auto before_error = service.error;
  if (!(state == ServiceState::kRunning && service.worker && !service.entered))
    service.state = state;
  service.error = error;
  if (before_state != service.state || before_error != error)
    events.Append(id, service.state, ServiceEventKind::kState, error, Now());
}
void ObserveBoot(ServiceId id, ServiceState state, std::int32_t error, void*)
{
  ObserveService(id, state, error);
  if (state == ServiceState::kFailed || state == ServiceState::kBlocked)
    ESP_LOGE("UG",
             "Service %s %s (error %ld); independent services retained",
             ServiceName(id),
             ServiceStateName(state),
             static_cast<long>(error));
}
ServiceHealthSnapshot ServiceHealths()
{
  std::lock_guard<std::mutex> lock(guard);
  return services;
}
ServiceEventSnapshot ServiceEvents()
{
  std::lock_guard<std::mutex> lock(guard);
  return events.Snapshot();
}
WorkerHealth::WorkerHealth(ServiceId id, std::int64_t deadline_ns, bool watchdog) : id_(id)
{
  if (!ValidService(id_))
    return;
  const auto error = watchdog ? esp_task_wdt_add(nullptr) : ESP_OK;
  watchdog_ = watchdog && error == ESP_OK;
  std::lock_guard<std::mutex> lock(guard);
  auto& service = services[static_cast<std::size_t>(id_)];
  service.state = ServiceState::kRunning;
  service.worker = true;
  service.entered = true;
  service.deadline_ns = deadline_ns;
  service.progress_ns = Now();
  service.watchdog_registered = watchdog_;
  service.watchdog_error = error;
  events.Append(id_, service.state, ServiceEventKind::kState, 0, service.progress_ns);
  if (error != ESP_OK)
    events.Append(id_, service.state, ServiceEventKind::kWatchdog, error, service.progress_ns);
}
WorkerHealth::~WorkerHealth()
{
  const auto removed = watchdog_ ? esp_task_wdt_delete(nullptr) : ESP_OK;
  if (!ValidService(id_))
    return;
  std::lock_guard<std::mutex> lock(guard);
  auto& service = services[static_cast<std::size_t>(id_)];
  if (removed == ESP_OK)
    service.watchdog_registered = false;
  else
    service.watchdog_error = removed;
  // Unexpected worker exit must not retain a cached healthy state.
  service.state = ServiceState::kFailed;
  service.error = ESP_ERR_INVALID_STATE;
  events.Append(id_, service.state, ServiceEventKind::kWorkerExit, service.error, Now());
  if (removed != ESP_OK)
    events.Append(id_, service.state, ServiceEventKind::kWatchdog, removed, Now());
}
void WorkerHealth::Progress()
{
  if (!ValidService(id_))
    return;
  const auto error = watchdog_ ? esp_task_wdt_reset() : ESP_OK;
  const auto now = Now();
  std::uint32_t stack = 0;
  // IDF FreeRTOS returns bytes. Scan at most once per second, outside the lock.
  const bool sampled = now >= last_stack_sample_ns_ && now - last_stack_sample_ns_ >= 1000000000LL;
  if (sampled)
  {
    stack = uxTaskGetStackHighWaterMark(nullptr);
    last_stack_sample_ns_ = now;
  }
  std::lock_guard<std::mutex> lock(guard);
  auto& service = services[static_cast<std::size_t>(id_)];
  service.progress_ns = now;
  if (error != ESP_OK)
  {
    if (service.watchdog_error != error)
      events.Append(id_, service.state, ServiceEventKind::kWatchdog, error, now);
    service.watchdog_error = error;
  }
  if (sampled)
  {
    service.stack_min_bytes = stack;
    service.stack_sampled = true;
  }
}
}  // namespace ug::embedded
