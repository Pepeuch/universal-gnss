// SPDX-License-Identifier: LGPL-3.0-only
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/opt.h"
#include "sdkconfig.h"
#include "ug_configuration.hpp"
#include "ug_gateway_service.hpp"
#include "ug_network_budget.hpp"
#include "ug_ntrip_connection.hpp"
#include "ug_ntrip_publisher.hpp"
#include "ug_observation.hpp"
#include "ug_services.hpp"
#include "ug_supervisor.hpp"
#include "ug_wait.hpp"
#include "universal_gnss_ntrip/local_rtcm_caster.hpp"
#include <atomic>
#include <mutex>
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
std::int64_t Now()
{
  return esp_timer_get_time() * 1000;
}
bool Current(const Gateway::Frame& f)
{
  return CorrectionSourceCurrent(f.source, f.incarnation) && Now() >= f.receipt_ns &&
         Now() - f.receipt_ns <= 3000000000LL;
}
struct Outbox
{
  QueueHandle_t queue{};
  StaticQueue_t control{};
  std::array<std::uint8_t, kQueueDepth * sizeof(Gateway::Frame)> storage{};
  std::size_t slot{};
  bool Create()
  {
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
void InputWorker(void*)
{
  WorkerHealth health(ServiceId::kNtripInput, 30000000000LL, false);
  std::uint64_t config_generation = 0, link_generation = 0;
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
  for (;;)
  {
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
  NtripPublisher publisher;
  Gateway::Frame frame;
  std::uint64_t config_generation = 0, link_generation = 0;
  std::size_t source = Gateway::kSources;
  std::uint64_t incarnation = 0;
  for (;;)
  {
    const auto config = Configuration();
    const auto link = network_generation.load();
    if (config_generation != config->revision || link_generation != link)
    {
      SetCorrectionDestination(1 + slot, nullptr, nullptr);
      xQueueReset(box.queue);
      publisher.Stop();
      source = Gateway::kSources;
      incarnation = 0;
      config_generation = config->revision;
      link_generation = link;
      const bool enabled = config->publishers[slot].enabled && network_ready.load();
      publisher.Configure(
          config->publishers[slot].endpoint, enabled, [config_generation, link_generation] {
            return !network_ready.load() || network_generation.load() != link_generation ||
                   Configuration()->revision != config_generation;
          });
      if (enabled)
        SetCorrectionDestination(1 + slot, Outbox::Enqueue, &box);
    }
    if (source < Gateway::kSources && !CorrectionSourceCurrent(source, incarnation))
    {
      publisher.Stop();
      xQueueReset(box.queue);
      source = Gateway::kSources;
      incarnation = 0;
      publisher.Configure(config->publishers[slot].endpoint,
                          config->publishers[slot].enabled && network_ready.load(),
                          [config_generation, link_generation] {
                            return !network_ready.load() ||
                                   network_generation.load() != link_generation ||
                                   Configuration()->revision != config_generation;
                          });
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
  universal_gnss_ntrip::LocalRtcmCaster caster;
  Gateway::Frame frame;
  std::uint64_t config_generation = 0, link_generation = 0, cache_generation = 0, incarnation = 0;
  std::size_t source = Gateway::kSources;
  for (;;)
  {
    const auto config = Configuration();
    const auto link = network_generation.load();
    if (config_generation != config->revision || link_generation != link)
    {
      SetCorrectionDestination(6, nullptr, nullptr);
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
esp_err_t StartNetworkServices()
{
  const auto& caps = BoardCapabilities();
  if (caps.publisher_slots > UG_SERVICE_PUBLISHERS || (caps.caster && !UG_SERVICE_CASTER) ||
      caps.publisher_slots + 1 > Gateway::kSinks)
    return ESP_ERR_INVALID_STATE;  // Never silently truncate a board capability.
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
}
}  // namespace ug::embedded
