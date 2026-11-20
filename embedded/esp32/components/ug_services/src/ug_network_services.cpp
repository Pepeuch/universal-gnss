// SPDX-License-Identifier: LGPL-3.0-only
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/opt.h"
#include "mbedtls/platform_util.h"
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include "esp_heap_caps.h"
#include "ug_queue_payload.hpp"
#endif
#include "ug_configuration.hpp"
#include "ug_exclusive_job.hpp"
#include "ug_gateway_service.hpp"
#include "ug_io_execution.hpp"
#include "ug_network_allocation.hpp"
#include "ug_network_stack.hpp"
#include "ug_s3_execution_policy.h"
#if CONFIG_IDF_TARGET_ESP32S3 && UG_NETWORK_STACK_PSRAM
#include "freertos/idf_additions.h"
#endif
#include "ug_network_budget.hpp"
#include "ug_ntrip_connection.hpp"
#include "ug_ntrip_publisher.hpp"
#include "ug_observation.hpp"
#include "ug_services.hpp"
#include "ug_supervisor.hpp"
#include "ug_wait.hpp"
#ifndef UG_NETWORK_COOPERATIVE
#define UG_NETWORK_COOPERATIVE 1
#endif
#include "universal_gnss_ntrip/local_rtcm_caster.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <new>
namespace ug::embedded
{
namespace
{
#ifdef CONFIG_MDNS_NETWORKING_SOCKET
constexpr bool kMdnsSocketMode = true;
#else
constexpr bool kMdnsSocketMode = false;
#endif
// Same private default-selection rules as the pinned SDK's lwIP core/dns.c.
// A dependency upgrade must review these rules and mDNS PCB ownership together.
constexpr std::size_t kDnsSourcePorts =
#if !LWIP_DNS
    0;
#elif (LWIP_DNS_SECURE & LWIP_DNS_SECURE_RAND_SRC_PORT) == 0
    1;
#elif defined(DNS_MAX_SOURCE_PORTS)
    DNS_MAX_SOURCE_PORTS;
#elif defined(DNS_MAX_REQUESTS) &&                                                                 \
    ((LWIP_DNS_SECURE & LWIP_DNS_SECURE_NO_MULTIPLE_OUTSTANDING) != 0)
    DNS_MAX_REQUESTS;
#else
    DNS_TABLE_SIZE;
#endif
constexpr NetworkCapacity kNetworkCapacity{RequiredNetworkBudget(UG_SERVICE_PUBLISHERS,
                                                                 UG_SERVICE_CASTER,
                                                                 CONFIG_MDNS_MAX_INTERFACES,
                                                                 kMdnsSocketMode,
                                                                 kDnsSourcePorts),
                                           CONFIG_LWIP_MAX_SOCKETS,
                                           CONFIG_LWIP_MAX_ACTIVE_TCP,
                                           CONFIG_LWIP_MAX_LISTENING_TCP,
                                           CONFIG_LWIP_MAX_UDP_PCBS};
static_assert(NetworkBudgetFits(kNetworkCapacity),
              "native network service limits exceed the configured SDK socket/PCB budget");
constexpr std::size_t kUpstream = 1, kQueueDepth = 2;
std::atomic<bool> network_ready{false};
std::atomic<std::uint64_t> network_generation{1}, upstream_generation{0};
std::mutex state_guard;
universal_gnss::GnssRuntimeState gnss_snapshot;
std::optional<std::int64_t> position_receipt_ns;
portMUX_TYPE metrics_guard = portMUX_INITIALIZER_UNLOCKED;
NetworkServiceStats stats;
NetworkTraceRing history;
std::int64_t Now()
{
  return esp_timer_get_time() * 1000;
}
struct TraceContext
{
  ServiceId service;
  const std::uint64_t& revision;
  const std::uint64_t& link;
  std::uint64_t attempt{};
  static void Observe(NtripTraceKind kind, NtripTraceError error, std::uint8_t code, void* context)
  {
    auto& owner = *static_cast<TraceContext*>(context);
    if (kind == NtripTraceKind::kConnectAttempt && owner.attempt != UINT64_MAX)
      ++owner.attempt;
    portENTER_CRITICAL(&metrics_guard);
    history.Append(
        {0, owner.revision, owner.link, owner.attempt, Now(), owner.service, kind, error, code});
    portEXIT_CRITICAL(&metrics_guard);
  }
  NtripTraceSink Sink()
  {
    return {Observe, this};
  }
};
bool Current(const Gateway::Frame& f)
{
  return CorrectionSourceCurrent(f.source, f.incarnation) && Now() >= f.receipt_ns &&
         Now() - f.receipt_ns <= 3000000000LL;
}
struct Outbox
{
  QueueHandle_t queue{};
  StaticQueue_t control{};
#if CONFIG_IDF_TARGET_ESP32S3
  QueuePayload<kQueueDepth * sizeof(Gateway::Frame)> storage;
#else
  std::array<std::uint8_t, kQueueDepth * sizeof(Gateway::Frame)> storage{};
#endif
  std::size_t slot{};
  bool Create()
  {
#if CONFIG_IDF_TARGET_ESP32S3
    // Called before publishing this queue to a worker/destination. Payload is
    // task-only, not DMA/ISR storage. Queue control remains internal. No free
    // while producers/consumers can hold the queue, including disabled services.
    if (!storage.Reserve([](std::size_t size) -> void* {
          return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }))
      return false;
#endif
    queue = xQueueCreateStatic(kQueueDepth, sizeof(Gateway::Frame), storage.data(), &control);
    return queue;
  }
  static bool Enqueue(const Gateway::Frame& frame, void* context)
  {
    auto& box = *static_cast<Outbox*>(context);
    return Current(frame) && xQueueSend(box.queue, &frame, 0) == pdTRUE;
  }
};
Outbox publishers[UG_SERVICE_PUBLISHERS];
#if UG_SERVICE_CASTER
Outbox caster_box;
#endif
#if CONFIG_IDF_TARGET_ESP32S3
// Control objects remain internal and survive every worker generation. Payload
// queues are allocated on first use and retained until reboot in this comparator.
// Only the manager starts a worker; only that worker releases its live flag,
// after all C++ locals and network owners have been destroyed.
struct WorkerSlot
{
  std::atomic<bool> live{false}, failed{false};
  std::size_t index{};
  std::int64_t retry_after_ns{};
};
std::array<WorkerSlot, 7> network_workers;
std::atomic<bool> lifecycle_started{false};
// Logical endpoint identity/filter/counters survive worker recreation. These
// task-only objects are reserved explicitly in external RAM on first activation;
// their sockets are closed on stop. No callback may use them from ISR context.
NtripConnection* input_owner{};
NtripPublisher* publisher_owners[UG_SERVICE_PUBLISHERS]{};
#if UG_SERVICE_CASTER
universal_gnss_ntrip::LocalRtcmCaster* caster_owner{};
std::uint64_t caster_cache_generation{};
#endif
template <class T, class Construct>
// This template can be called directly by the independent task manager: keep
// its placement-construction handler out of the Xtensa task-entry frame too.
__attribute__((noinline)) bool ReserveOwner(T*& owner, Construct construct)
{
  if (owner)
    return true;
  void* memory = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory)
    return false;
  try
  {
    owner = construct(memory);
    return true;
  }
  catch (const std::bad_alloc&)
  {
    // Placement construction may allocate existing std::string/Pimpl children.
    // Unwinding destroys completed members; the external object block is ours.
    mbedtls_platform_zeroize(memory, sizeof(T));
    heap_caps_free(memory);
    return false;
  }
  catch (...)
  {
    mbedtls_platform_zeroize(memory, sizeof(T));
    heap_caps_free(memory);
    throw;
  }
}

bool EndpointRequested(std::size_t index)
{
  const auto config = Configuration();
  if (!network_ready.load())
    return false;
  if (index == 0)
    return config->upstream_enabled && !config->local_input;
  if (index <= UG_SERVICE_PUBLISHERS)
    return index <= BoardCapabilities().publisher_slots && config->publishers[index - 1].enabled;
#if UG_SERVICE_CASTER
  if (index == 6)
    return BoardCapabilities().caster && config->caster_enabled;
#endif
  return false;
}
void QuiesceDestination(std::size_t sink, ServiceId id, WorkerHealth& health)
{
  // Failure to obtain retirement is not permission to free/reset the context.
  // Keep the owner alive and visible until the router acknowledges its withdrawal.
  bool blocked = false;
  while (!RetireCorrectionDestination(sink, 100))
  {
    blocked = true;
    ObserveService(id, ServiceState::kBlocked, ESP_ERR_TIMEOUT);
    health.Progress();
  }
  if (blocked)
    ObserveService(id, ServiceState::kRunning);
}
#endif
#if !CONFIG_IDF_TARGET_ESP32S3 || !UG_NETWORK_COOPERATIVE
void InputWorker(void*)
{
  WorkerHealth health(ServiceId::kNtripInput, 30000000000LL, false);
  std::uint64_t config_generation = 0, link_generation = 0;
#if CONFIG_IDF_TARGET_ESP32S3
  auto& input = *input_owner;
#else
  NtripConnection input(
      [](const auto& f) {
        const auto epoch = upstream_generation.load();
        if (epoch && f.timestamp_ns)
          SubmitCorrectionBytes(
              kUpstream, epoch, f.raw_bytes.data(), f.raw_bytes.size(), *f.timestamp_ns, true);
      },
      [](bool active) {
        upstream_generation = BeginCorrectionSource(kUpstream, active && network_ready.load());
      });
#endif
  TraceContext trace{ServiceId::kNtripInput, config_generation, link_generation};
  input.SetTrace(trace.Sink());
  for (;;)
  {
#if CONFIG_IDF_TARGET_ESP32S3
    if (!EndpointRequested(0))
    {
      input.Stop();
      upstream_generation = BeginCorrectionSource(kUpstream, false);
      health.Finish();
      return;
    }
#endif
    const auto config = Configuration();
    const auto link = network_generation.load();
    if (config->revision != config_generation || link != link_generation)
    {
      input.Stop();
      upstream_generation = BeginCorrectionSource(kUpstream, false);
      config_generation = config->revision;
      link_generation = link;
      input.Configure(config->upstream,
                      config->upstream_enabled && !config->local_input && network_ready.load(),
                      [config_generation, link_generation] {
                        return !network_ready.load() ||
                               network_generation.load() != link_generation ||
                               Configuration()->revision != config_generation;
                      });
    }
    universal_gnss::GnssRuntimeState position;
    {
      std::lock_guard<std::mutex> lock(state_guard);
      position = FreshPositionForGga(gnss_snapshot, position_receipt_ns, Now());
    }
    input.Tick(Now(), &position);
    const auto& metrics = input.client().metrics();
    NetworkOwnerStats sample;
    sample.configuration_revision = config_generation;
    sample.link_generation = link_generation;
    sample.receipt_ns = Now();
    sample.sampled = true;
    sample.active = input.client().IsConnected();
    sample.frames = metrics.rtcm_frames_received;
    sample.bytes = metrics.bytes_received;
    sample.connections = metrics.reconnect_count;
    const bool flowing = input.client().IsCorrectionFlowing();
    portENTER_CRITICAL(&metrics_guard);
    stats.upstream = sample;
    stats.upstream_flowing = flowing;
    stats.upstream_error = static_cast<std::uint32_t>(metrics.last_error);
    stats.upstream_gga_sent = metrics.gga_sent_count;
    stats.upstream_gga_errors = metrics.gga_send_errors;
    portEXIT_CRITICAL(&metrics_guard);
    health.Progress();
    vTaskDelay(FiniteWaitTicks(10, configTICK_RATE_HZ));
  }
}
void PublisherWorker(void* context)
{
  auto& box = *static_cast<Outbox*>(context);
  const auto slot = box.slot;
  WorkerHealth health(static_cast<ServiceId>(static_cast<unsigned>(ServiceId::kPublisher0) + slot),
                      30000000000LL,
                      false);
#if CONFIG_IDF_TARGET_ESP32S3
  auto& publisher = *publisher_owners[slot];
#else
  NtripPublisher publisher;
#endif
  Gateway::Frame frame;
  std::uint64_t config_generation = 0, link_generation = 0;
  TraceContext trace{static_cast<ServiceId>(static_cast<unsigned>(ServiceId::kPublisher0) + slot),
                     config_generation,
                     link_generation};
  publisher.SetTrace(trace.Sink());
  std::size_t source = Gateway::kSources;
  std::uint64_t incarnation = 0;
  for (;;)
  {
#if CONFIG_IDF_TARGET_ESP32S3
    if (!EndpointRequested(1 + slot))
    {
      publisher.Stop();
      QuiesceDestination(
          1 + slot,
          static_cast<ServiceId>(static_cast<unsigned>(ServiceId::kPublisher0) + slot),
          health);
      xQueueReset(box.queue);
      health.Finish();
      return;
    }
#endif
    const auto config = Configuration();
    const auto link = network_generation.load();
    if (config_generation != config->revision || link_generation != link)
    {
#if CONFIG_IDF_TARGET_ESP32S3
      QuiesceDestination(
          1 + slot,
          static_cast<ServiceId>(static_cast<unsigned>(ServiceId::kPublisher0) + slot),
          health);
#else
      SetCorrectionDestination(1 + slot, nullptr, nullptr);
#endif
      xQueueReset(box.queue);
      publisher.Stop();
      source = Gateway::kSources;
      incarnation = 0;
      config_generation = config->revision;
      link_generation = link;
      const bool enabled = config->publishers[slot].enabled && network_ready.load();
      const bool configured = publisher.Configure(
          config->publishers[slot].endpoint,
          enabled,
          [config_generation, link_generation] {
            return !network_ready.load() || network_generation.load() != link_generation ||
                   Configuration()->revision != config_generation;
          },
          config->publishers[slot].rtcm_messages);
      if (enabled && configured)
        SetCorrectionDestination(1 + slot, Outbox::Enqueue, &box);
    }
    if (source < Gateway::kSources && !CorrectionSourceCurrent(source, incarnation))
    {
      publisher.Stop();
      xQueueReset(box.queue);
      source = Gateway::kSources;
      incarnation = 0;
      publisher.Configure(
          config->publishers[slot].endpoint,
          config->publishers[slot].enabled && network_ready.load(),
          [config_generation, link_generation] {
            return !network_ready.load() || network_generation.load() != link_generation ||
                   Configuration()->revision != config_generation;
          },
          config->publishers[slot].rtcm_messages);
    }
    publisher.Tick(Now());
    // Consume even while disconnected, so a reconnect never drains an old backlog.
    while (xQueueReceive(box.queue, &frame, 0) == pdTRUE)
    {
      if (!Current(frame) || !publisher.streaming())
        continue;
      if (source == Gateway::kSources)
      {
        source = frame.source;
        incarnation = frame.incarnation;
      }
      if (source != frame.source || incarnation != frame.incarnation)
      {
        publisher.Stop();
        break;
      }
      (void)publisher.Send(frame, Now());
    }
    const auto& metrics = publisher.metrics();
    NetworkOwnerStats sample;
    sample.configuration_revision = config_generation;
    sample.link_generation = link_generation;
    sample.receipt_ns = Now();
    sample.sampled = true;
    sample.active = publisher.streaming();
    sample.frames = metrics.frames;
    sample.bytes = metrics.bytes;
    sample.connections = metrics.connects;
    sample.failures = metrics.failures;
    sample.expired = metrics.expired;
    portENTER_CRITICAL(&metrics_guard);
    stats.publishers[slot] = sample;
    portEXIT_CRITICAL(&metrics_guard);
    health.Progress();
    vTaskDelay(FiniteWaitTicks(10, configTICK_RATE_HZ));
  }
}
#if UG_SERVICE_CASTER
void CasterWorker(void*)
{
  WorkerHealth health(ServiceId::kCaster, 30000000000LL, false);
#if CONFIG_IDF_TARGET_ESP32S3
  auto& caster = *caster_owner;
#else
  universal_gnss_ntrip::LocalRtcmCaster caster;
#endif
  Gateway::Frame frame;
  std::uint64_t config_generation = 0, link_generation = 0, incarnation = 0;
#if CONFIG_IDF_TARGET_ESP32S3
  // The retained caster rejects a reused source incarnation after Stop().
  auto& cache_generation = caster_cache_generation;
#else
  std::uint64_t cache_generation = 0;
#endif
  std::size_t source = Gateway::kSources;
  for (;;)
  {
#if CONFIG_IDF_TARGET_ESP32S3
    if (!EndpointRequested(6))
    {
      caster.Stop();
      QuiesceDestination(6, ServiceId::kCaster, health);
      xQueueReset(caster_box.queue);
      health.Finish();
      return;
    }
#endif
    const auto config = Configuration();
    const auto link = network_generation.load();
    if (config_generation != config->revision || link_generation != link)
    {
#if CONFIG_IDF_TARGET_ESP32S3
      QuiesceDestination(6, ServiceId::kCaster, health);
#else
      SetCorrectionDestination(6, nullptr, nullptr);
#endif
      caster.Stop();
      xQueueReset(caster_box.queue);
      source = Gateway::kSources;
      incarnation = 0;
      config_generation = config->revision;
      link_generation = link;
      if (config->caster_enabled && network_ready.load())
      {
        universal_gnss_ntrip::LocalRtcmCasterPolicy policy;
        policy.max_clients = kCasterClientLimit;
        policy.max_accepts_per_poll = 2;
        policy.request_timeout_ms = 5000;
        policy.disconnect_clients_on_source_change = true;
        policy.username = config->caster_user;
        policy.password = config->caster_password;
        universal_gnss_ntrip::LocalRtcmCasterConfig cfg;
        cfg.bind_host = "0.0.0.0";
        cfg.port = config->caster_port;
        cfg.mountpoint = config->caster_mountpoint;
        cfg.client_buffer_bytes = 4096;
        if (caster.SetPolicy(policy) && caster.Start(cfg))
          SetCorrectionDestination(6, Outbox::Enqueue, &caster_box);
      }
    }
    if (source < Gateway::kSources && !CorrectionSourceCurrent(source, incarnation))
    {
      caster.EndSource();
      source = Gateway::kSources;
      incarnation = 0;
      xQueueReset(caster_box.queue);
    }
    while (caster.running() && xQueueReceive(caster_box.queue, &frame, 0) == pdTRUE)
    {
      if (!Current(frame))
        continue;
      if (source != frame.source || incarnation != frame.incarnation)
      {
        caster.EndSource();
        source = frame.source;
        incarnation = frame.incarnation;
        if (cache_generation == UINT64_MAX ||
            !caster.ActivateSource({"ug-selected-correction", ++cache_generation}))
          continue;
      }
      universal_gnss_protocols::RtcmFrame f;
      f.raw_bytes.assign(frame.bytes.begin(), frame.bytes.begin() + frame.size);
      f.checksum_status = universal_gnss_protocols::ChecksumStatus::kValid;
      f.timestamp_ns = frame.receipt_ns;
      if (frame.size >= 8)
        f.message_type = (std::uint16_t(frame.bytes[3]) << 4) | (frame.bytes[4] >> 4);
      (void)caster.PublishFrame(f);
    }
    caster.Poll();
    NetworkOwnerStats sample;
    sample.configuration_revision = config_generation;
    sample.link_generation = link_generation;
    sample.receipt_ns = Now();
    sample.sampled = true;
    sample.active = caster.running();
    sample.frames = caster.metrics().served_frames;
    sample.connections = caster.metrics().accepted_clients;
    sample.failures = caster.metrics().rejected_requests;
    sample.expired = caster.metrics().slow_client_disconnects;
    portENTER_CRITICAL(&metrics_guard);
    stats.caster = sample;
    portEXIT_CRITICAL(&metrics_guard);
    health.Progress();
    vTaskDelay(FiniteWaitTicks(5, configTICK_RATE_HZ));
  }
}
#endif
#endif  // Legacy independent execution comparator and C3 path.
#if CONFIG_IDF_TARGET_ESP32S3 && UG_NETWORK_COOPERATIVE
#include "ug_network_cooperative.hpp"
#elif CONFIG_IDF_TARGET_ESP32S3
// Keep exception handlers above the sentinel return address of a task entry.
__attribute__((noinline)) std::int32_t GuardedNetworkWorker(WorkerSlot& slot)
{
  std::int32_t error = ESP_OK;
  try
  {
    if (slot.index == 0)
      InputWorker(nullptr);
    else if (slot.index <= UG_SERVICE_PUBLISHERS)
      PublisherWorker(&publishers[slot.index - 1]);
#if UG_SERVICE_CASTER
    else if (slot.index == 6)
      CasterWorker(nullptr);
#endif
  }
  catch (const std::bad_alloc&)
  {
    error = ESP_ERR_NO_MEM;
  }
  catch (...)
  {
    error = ESP_FAIL;
  }
  return error;
}
void NetworkWorker(void* context)
{
  auto& slot = *static_cast<WorkerSlot*>(context);
  const auto error = GuardedNetworkWorker(slot);
  if (error != ESP_OK)
  {
    // Trace contexts were stack-owned and have unwound. Detach before Stop emits
    // an event, then quiesce without allocating or using a destroyed guard.
    if (slot.index == 0)
    {
      input_owner->SetTrace({});
      input_owner->Stop();
      upstream_generation = BeginCorrectionSource(kUpstream, false);
    }
    else
    {
      if (slot.index <= UG_SERVICE_PUBLISHERS)
      {
        publisher_owners[slot.index - 1]->SetTrace({});
        publisher_owners[slot.index - 1]->Stop();
      }
#if UG_SERVICE_CASTER
      else
        caster_owner->Stop();
#endif
      while (!RetireCorrectionDestination(slot.index, 100))
        vTaskDelay(FiniteWaitTicks(10, configTICK_RATE_HZ));
      if (slot.index <= UG_SERVICE_PUBLISHERS)
        xQueueReset(publishers[slot.index - 1].queue);
#if UG_SERVICE_CASTER
      else
        xQueueReset(caster_box.queue);
#endif
    }
    const auto id = slot.index == 0 ? ServiceId::kNtripInput
                    : slot.index == 6
                        ? ServiceId::kCaster
                        : static_cast<ServiceId>(static_cast<unsigned>(ServiceId::kPublisher0) +
                                                 slot.index - 1);
    ObserveService(id, ServiceState::kFailed, error);
    slot.failed.store(true, std::memory_order_release);
  }
  // Owner scopes have returned, sockets closed, and destinations retired.
  // No access to this slot follows publication; RTOS reaps the stack on idle.
  slot.live.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}
void NetworkLifecycle(void*)
{
  WorkerHealth manager(ServiceId::kNtripSetup, 5000000000LL, false);
  for (;;)
  {
    for (auto& slot : network_workers)
    {
      if (slot.live.load(std::memory_order_acquire))
        continue;
      if (slot.failed.exchange(false, std::memory_order_acquire))
        slot.retry_after_ns = Now() + 1000000000LL;
      if (!EndpointRequested(slot.index) || Now() < slot.retry_after_ns)
        continue;
      const auto id = slot.index == 0 ? ServiceId::kNtripInput
                      : slot.index == 6
                          ? ServiceId::kCaster
                          : static_cast<ServiceId>(static_cast<unsigned>(ServiceId::kPublisher0) +
                                                   slot.index - 1);
      bool ready = true;
      if (slot.index == 0)
        ready = ReserveOwner(input_owner, [](void* memory) {
          return new (memory) NtripConnection(
              [](const auto& f) {
                const auto epoch = upstream_generation.load();
                if (epoch && f.timestamp_ns)
                  SubmitCorrectionBytes(kUpstream,
                                        epoch,
                                        f.raw_bytes.data(),
                                        f.raw_bytes.size(),
                                        *f.timestamp_ns,
                                        true);
              },
              [](bool active) {
                upstream_generation =
                    BeginCorrectionSource(kUpstream, active && network_ready.load());
              });
        });
      if (slot.index > 0 && slot.index <= UG_SERVICE_PUBLISHERS)
      {
        auto& box = publishers[slot.index - 1];
        box.slot = slot.index - 1;
        ready = ReserveOwner(publisher_owners[slot.index - 1],
                             [](void* memory) { return new (memory) NtripPublisher{}; }) &&
                (box.queue || box.Create());
      }
#if UG_SERVICE_CASTER
      else if (slot.index == 6)
        ready = ReserveOwner(caster_owner,
                             [](void* memory) {
                               return new (memory) universal_gnss_ntrip::LocalRtcmCaster{};
                             }) &&
                (caster_box.queue || caster_box.Create());
#endif
      ExpectWorker(id, 30000000000LL);
      slot.live.store(true, std::memory_order_release);
      if (!ready || xTaskCreate(NetworkWorker,
                                slot.index == 0   ? "ug_ntrip_in"
                                : slot.index == 6 ? "ug_caster"
                                                  : "ug_ntrip_out",
                                slot.index == 0 ? 10240 : 8192,
                                &slot,
                                slot.index == 0 ? 3 : 2,
                                nullptr) != pdPASS)
      {
        slot.live.store(false, std::memory_order_release);
        slot.retry_after_ns = Now() + 1000000000LL;
        ObserveService(id, ServiceState::kFailed, ESP_ERR_NO_MEM);
      }
    }
    manager.Progress();
    vTaskDelay(FiniteWaitTicks(20, configTICK_RATE_HZ));
  }
}
#endif
}  // namespace
NetworkCapacity NetworkResourceCapacity()
{
  return kNetworkCapacity;
}
void NetworkAvailabilityChanged(bool available)
{
  network_ready = available;
  network_generation.fetch_add(1);
  if (!available)
    upstream_generation = BeginCorrectionSource(kUpstream, false);
}
void RestartNetworkServices()
{
  upstream_generation = BeginCorrectionSource(kUpstream, false);
  network_generation.fetch_add(1);
}
void PublishGnssSnapshot(const universal_gnss::GnssRuntimeState& state,
                         std::optional<std::int64_t> receipt)
{
  std::lock_guard<std::mutex> lock(state_guard);
  gnss_snapshot = state;
  position_receipt_ns = receipt;
}
universal_gnss::GnssRuntimeState GnssSnapshot()
{
  std::lock_guard<std::mutex> lock(state_guard);
  return gnss_snapshot;
}
GnssObservationSnapshot GnssObservation()
{
  std::lock_guard<std::mutex> lock(state_guard);
  return {gnss_snapshot, position_receipt_ns};
}
NetworkServiceStats NetworkStats()
{
  portENTER_CRITICAL(&metrics_guard);
  const auto out = stats;
  portEXIT_CRITICAL(&metrics_guard);
  return ProjectNetworkStats(
      out, Configuration()->revision, network_generation.load(), network_ready.load(), Now());
}
NetworkTraceSnapshot NetworkHistory()
{
  portENTER_CRITICAL(&metrics_guard);
  const auto out = history.Snapshot();
  portEXIT_CRITICAL(&metrics_guard);
  return out;
}
esp_err_t StartNetworkServices()
{
  const auto& caps = BoardCapabilities();
  if (caps.publisher_slots > UG_SERVICE_PUBLISHERS || (caps.caster && !UG_SERVICE_CASTER) ||
      caps.publisher_slots + 1 > Gateway::kSinks)
    return ESP_ERR_INVALID_STATE;  // Never silently truncate a board capability.
#if CONFIG_IDF_TARGET_ESP32S3
  bool expected = false;
  if (!lifecycle_started.compare_exchange_strong(expected, true))
    return ESP_ERR_INVALID_STATE;
  if (!InitializeNetworkAllocation())
  {
    lifecycle_started = false;
    return ESP_ERR_NO_MEM;
  }
#if !UG_NETWORK_COOPERATIVE
  for (std::size_t i = 0; i < network_workers.size(); ++i)
    network_workers[i].index = i;
#endif
#if UG_NETWORK_COOPERATIVE && UG_NETWORK_STACK_PSRAM && UG_S3_CPU_PROFILE > 0
  const bool started =
      xTaskCreatePinnedToCoreWithCaps(NetworkLifecycle,
                                      "ug_network_life",
                                      12288,
                                      nullptr,
                                      2,
                                      nullptr,
                                      ug_s3_worker_core(UG_S3_WORKER_NETWORK),
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
#elif UG_NETWORK_COOPERATIVE && UG_NETWORK_STACK_PSRAM
  const bool started = xTaskCreateWithCaps(NetworkLifecycle,
                                           "ug_network_life",
                                           12288,
                                           nullptr,
                                           2,
                                           nullptr,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
#elif UG_S3_CPU_PROFILE > 0
  const bool started = xTaskCreatePinnedToCore(NetworkLifecycle,
                                               "ug_network_life",
                                               UG_NETWORK_COOPERATIVE ? 12288 : 4096,
                                               nullptr,
                                               2,
                                               nullptr,
                                               ug_s3_worker_core(UG_S3_WORKER_NETWORK)) == pdPASS;
#else
  const bool started = xTaskCreate(NetworkLifecycle,
                                   "ug_network_life",
                                   UG_NETWORK_COOPERATIVE ? 12288 : 4096,
                                   nullptr,
                                   2,
                                   nullptr) == pdPASS;
#endif
  if (started)
    return ESP_OK;
  lifecycle_started = false;
  return ESP_ERR_NO_MEM;
#else
  bool ok = true;
  ExpectWorker(ServiceId::kNtripInput, 30000000000LL);
  if (xTaskCreate(InputWorker, "ug_ntrip_in", 10240, nullptr, 3, nullptr) != pdPASS)
  {
    ObserveService(ServiceId::kNtripInput, ServiceState::kFailed, ESP_ERR_NO_MEM);
    ok = false;
  }
  for (std::size_t i = 0; i < caps.publisher_slots; ++i)
  {
    const auto id = static_cast<ServiceId>(static_cast<unsigned>(ServiceId::kPublisher0) + i);
    ExpectWorker(id, 30000000000LL);
    auto& box = publishers[i];
    box.slot = i;
    if (!box.Create() ||
        xTaskCreate(PublisherWorker, "ug_ntrip_out", 8192, &box, 2, nullptr) != pdPASS)
    {
      ObserveService(id, ServiceState::kFailed, ESP_ERR_NO_MEM);
      ok = false;  // One failed publisher must not skip remaining independent slots.
    }
  }
#if UG_SERVICE_CASTER
  if (caps.caster)
  {
    ExpectWorker(ServiceId::kCaster, 30000000000LL);
    if (!caster_box.Create() ||
        xTaskCreate(CasterWorker, "ug_caster", 8192, nullptr, 2, nullptr) != pdPASS)
    {
      ObserveService(ServiceId::kCaster, ServiceState::kFailed, ESP_ERR_NO_MEM);
      ok = false;
    }
  }
#endif
  return ok ? ESP_OK : ESP_ERR_NO_MEM;
#endif
}
}  // namespace ug::embedded
