// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace ug::embedded
{
enum class ServiceId : std::uint8_t
{
  kConfiguration,
  kNetworkCore,
  kRouting,
  kNtripInput,
  kPublisher0,
  kPublisher1,
  kPublisher2,
  kPublisher3,
  kPublisher4,
  kCaster,
  kGnss,
  kEthernet,
  kWifi,
  kRadio,
  kRadioControl,
  kConsole,
  kHttps,
  kNtripSetup,
  kCount
};
constexpr std::size_t kServiceCount = static_cast<std::size_t>(ServiceId::kCount);
static_assert(kServiceCount < 32);
constexpr bool ValidService(ServiceId id)
{
  return static_cast<std::size_t>(id) < kServiceCount;
}
constexpr std::uint32_t ServiceBit(ServiceId id)
{
  return ValidService(id) ? (std::uint32_t{1} << static_cast<unsigned>(id)) : 0;
}
inline const char* ServiceName(ServiceId id)
{
  constexpr std::array<const char*, kServiceCount> names{"configuration",
                                                         "network_core",
                                                         "rtcm_router",
                                                         "ntrip_input",
                                                         "publisher_0",
                                                         "publisher_1",
                                                         "publisher_2",
                                                         "publisher_3",
                                                         "publisher_4",
                                                         "caster",
                                                         "gnss_owner",
                                                         "ethernet_driver",
                                                         "wifi_time_setup",
                                                         "espnow_driver",
                                                         "radio_control",
                                                         "local_console",
                                                         "https",
                                                         "ntrip_setup"};
  return ValidService(id) ? names[static_cast<std::size_t>(id)] : "invalid";
}
enum class ServiceState : std::uint8_t
{
  kDisabled,
  kStarting,
  kRunning,
  kFailed,
  kBlocked
};
inline const char* ServiceStateName(ServiceState state)
{
  switch (state)
  {
    case ServiceState::kDisabled:
      return "disabled";
    case ServiceState::kStarting:
      return "starting";
    case ServiceState::kRunning:
      return "running";
    case ServiceState::kFailed:
      return "failed";
    case ServiceState::kBlocked:
      return "blocked";
  }
  return "invalid";
}
struct ServiceHealth
{
  ServiceState state{ServiceState::kDisabled};
  std::int32_t error{}, watchdog_error{};
  std::int64_t progress_ns{}, deadline_ns{};
  std::uint32_t stack_min_bytes{};
  bool worker{}, entered{}, stack_sampled{}, watchdog_registered{};
};
inline bool ServiceStale(const ServiceHealth& service, std::int64_t now_ns)
{
  return service.worker &&
         (service.state == ServiceState::kRunning || service.state == ServiceState::kStarting) &&
         (service.deadline_ns <= 0 || service.progress_ns < 0 || now_ns < service.progress_ns ||
          now_ns - service.progress_ns > service.deadline_ns);
}
using ServiceHealthSnapshot = std::array<ServiceHealth, kServiceCount>;
inline bool ServicesDegraded(const ServiceHealthSnapshot& services, std::int64_t now_ns)
{
  for (const auto& s : services)
    if (s.state == ServiceState::kFailed || s.state == ServiceState::kBlocked ||
        s.watchdog_error != 0 || ServiceStale(s, now_ns))
      return true;
  return false;
}
// Fixed boot plan: failure blocks only explicit dependents. No retry, rollback,
// heap allocation, asynchronous callbacks or retained pointers to stack contexts.
struct BootStep
{
  ServiceId id;
  std::uint32_t dependencies;
  std::int32_t (*start)(void*);
  void* context{};
  bool enabled{true};
};
using BootObserver = void (*)(ServiceId, ServiceState, std::int32_t, void*);
struct BootResult
{
  std::uint32_t started{}, failed{}, blocked{};
  bool plan_valid{true};
};
inline BootResult
RunBoot(const BootStep* steps, std::size_t count, BootObserver observe, void* context = nullptr)
{
  BootResult result;
  if (!steps || count > kServiceCount)
  {
    result.plan_valid = false;
    return result;
  }
  std::uint32_t seen = 0;
  for (std::size_t i = 0; i < count; ++i)
  {
    const auto bit = ServiceBit(steps[i].id);
    if (!bit || (seen & bit))
    {
      result.plan_valid = false;
      return result;
    }
    seen |= bit;
  }
  for (std::size_t i = 0; i < count; ++i)
  {
    const auto& step = steps[i];
    const auto bit = ServiceBit(step.id);
    if (!step.enabled)
    {
      if (observe)
        observe(step.id, ServiceState::kDisabled, 0, context);
      continue;
    }
    if (!step.start || (step.dependencies & result.started) != step.dependencies)
    {
      result.blocked |= bit;
      if (observe)
        observe(step.id, ServiceState::kBlocked, -1, context);
      continue;
    }
    if (observe)
      observe(step.id, ServiceState::kStarting, 0, context);
    const auto error = step.start(step.context);
    if (error)
      result.failed |= bit;
    else
      result.started |= bit;
    if (observe)
      observe(step.id, error ? ServiceState::kFailed : ServiceState::kRunning, error, context);
  }
  return result;
}
}  // namespace ug::embedded
