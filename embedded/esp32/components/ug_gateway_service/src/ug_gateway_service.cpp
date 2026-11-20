// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_gateway_service.hpp"
#include "sdkconfig.h"
#include "ug_destination_lifecycle.hpp"
#include "ug_s3_execution_policy.h"
#include "ug_supervisor.hpp"
#include "ug_wait.hpp"
extern "C" {
#include "espnow_rtcm.h"
}
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <array>
#include <cstring>
#if CONFIG_IDF_TARGET_ESP32S3
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "ug_boot_payload.hpp"
#include <cstdio>
#endif
namespace ug::embedded
{
namespace
{
struct Input
{
  std::array<std::uint8_t, Gateway::kMaxFrame> bytes{};
  std::size_t source{}, size{};
  std::uint64_t incarnation{};
  std::int64_t receipt_ns{};
  bool complete{};
};
struct Control
{
  std::uint64_t epoch{};
  bool enabled{};
};
using Publication = DestinationPublication<FrameDestination>;
using Destination = Publication::Target;
std::array<Control, Gateway::kSources> desired{}, applied{};
std::array<Publication, Gateway::kSinks> destinations{};
std::array<Destination, Gateway::kSinks> active_destinations{};
GatewayServiceStats stats{};
portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t ingress;
StaticQueue_t ingress_control;
constexpr std::size_t kIngressDepth = 8;
#if CONFIG_IDF_TARGET_ESP32S3
// Task-only routing data, never DMA/ISR or cache-disabled storage. FreeRTOS
// queue control, locks and source/destination publication stay internal above.
struct RoutingPayload
{
  Gateway gateway;
  Gateway::Frame frame;
  Input input;
  std::array<std::uint8_t, kIngressDepth * sizeof(Input)> ingress_memory{};
};
BootPayload<RoutingPayload> routing_payload;
bool InitializeRoutingPayload()
{
  return routing_payload.Initialize([](std::size_t size) -> void* {
    auto* memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const bool external = memory && esp_ptr_external_ram(memory);
    if (memory && !external)
    {
      heap_caps_free(memory);
      memory = nullptr;
    }
    std::printf("UG_MEM routing_storage bytes=%zu external=%d ready=%d\n",
                size,
                external,
                memory != nullptr);
    return memory;
  });
}
Gateway& RoutingGateway()
{
  return routing_payload.Get()->gateway;
}
Gateway::Frame& RoutingFrame()
{
  return routing_payload.Get()->frame;
}
Input& RoutingInput()
{
  return routing_payload.Get()->input;
}
std::uint8_t* IngressMemory()
{
  return routing_payload.Get()->ingress_memory.data();
}
#else
Gateway gateway;
Gateway::Frame frame;
std::array<std::uint8_t, kIngressDepth * sizeof(Input)> ingress_memory{};
Input input;
Gateway& RoutingGateway()
{
  return gateway;
}
Gateway::Frame& RoutingFrame()
{
  return frame;
}
Input& RoutingInput()
{
  return input;
}
std::uint8_t* IngressMemory()
{
  return ingress_memory.data();
}
#endif
void RefreshControls(Gateway& gateway)
{
  for (std::size_t i = 0; i < desired.size(); ++i)
  {
    portENTER_CRITICAL(&guard);
    const auto control = desired[i];
    portEXIT_CRITICAL(&guard);
    if (control.epoch == applied[i].epoch)
      continue;
    gateway.ConfigureSource(i, control.epoch, control.enabled);
    applied[i] = control;
    espnow_rtcm_invalidate_pending();
  }
}
void RefreshDestinations(Gateway& gateway)
{
  for (std::size_t i = 0; i < destinations.size(); ++i)
  {
    portENTER_CRITICAL(&guard);
    const auto target = destinations[i].Desired();
    portEXIT_CRITICAL(&guard);
    if (target.revision != active_destinations[i].revision)
    {
      gateway.ConfigureSink(i, target.callback != nullptr, 3000000000LL);
      active_destinations[i] = target;
      // This owner is the only callback executor. Its previous iteration has
      // completed before this replacement; no retired callback remains in flight.
      portENTER_CRITICAL(&guard);
      (void)destinations[i].Acknowledge(target.revision);
      portEXIT_CRITICAL(&guard);
    }
  }
}
void Owner(void*)
{
#if CONFIG_IDF_TARGET_ESP32S3 && defined(ESP_PLATFORM)
  std::printf("UG_CPU role=router profile=%d configured_core=%d observed_core=%d\n",
              UG_S3_CPU_PROFILE,
              ug_s3_worker_core(UG_S3_WORKER_ROUTER),
              xPortGetCoreID());
#endif
  auto& gateway = RoutingGateway();
  auto& frame = RoutingFrame();
  auto& input = RoutingInput();
  WorkerHealth health(ServiceId::kRouting, 5000000000LL, true);
  for (;;)
  {
    RefreshControls(gateway);
    RefreshDestinations(gateway);
    if (xQueueReceive(ingress, &input, FiniteWaitTicks(5, configTICK_RATE_HZ)) == pdTRUE)
    {
      RefreshControls(gateway);
      if (input.complete)
        gateway.FeedFrame(
            input.source, input.incarnation, input.bytes.data(), input.size, input.receipt_ns);
      else
        gateway.Feed(
            input.source, input.incarnation, input.bytes.data(), input.size, input.receipt_ns);
    }
    RefreshControls(gateway);
    for (std::size_t i = 0; i < Gateway::kSinks; ++i)
    {
      while (gateway.Pop(i, esp_timer_get_time() * 1000, frame))
      {
        const auto target = active_destinations[i];
        if (target.callback && !target.callback(frame, target.context))
        {
          portENTER_CRITICAL(&guard);
          ++stats.destination_dropped;
          portEXIT_CRITICAL(&guard);
        }
      }
    }
    portENTER_CRITICAL(&guard);
    stats.routing = gateway.metrics();
    portEXIT_CRITICAL(&guard);
    health.Progress();
  }
}
}  // namespace
esp_err_t StartGatewayService()
{
  if (ingress)
    return ESP_ERR_INVALID_STATE;
#if CONFIG_IDF_TARGET_ESP32S3
  if (!InitializeRoutingPayload())
    return ESP_ERR_NO_MEM;
#endif
  ingress = xQueueCreateStatic(kIngressDepth, sizeof(Input), IngressMemory(), &ingress_control);
  if (!ingress)
    return ESP_ERR_NO_MEM;

  ExpectWorker(ServiceId::kRouting, 5000000000LL);
#if CONFIG_IDF_TARGET_ESP32S3 && UG_S3_CPU_PROFILE > 0
  const bool started =
      xTaskCreatePinnedToCore(
          Owner, "ug_rtcm", 6144, nullptr, 5, nullptr, ug_s3_worker_core(UG_S3_WORKER_ROUTER)) ==
      pdPASS;
#else
  const bool started = xTaskCreate(Owner, "ug_rtcm", 6144, nullptr, 5, nullptr) == pdPASS;
#endif
  if (!started)
  {
    vQueueDelete(ingress);
    ingress = nullptr;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
std::uint64_t BeginCorrectionSource(const std::size_t source, const bool enabled)
{
  if (source >= desired.size())
    return 0;
  portENTER_CRITICAL(&guard);
  auto& control = desired[source];
  if (control.epoch == UINT64_MAX)
  {
    portEXIT_CRITICAL(&guard);
    return 0;
  }
  ++control.epoch;
  control.enabled = enabled;
  const auto epoch = control.epoch;
  portEXIT_CRITICAL(&guard);
  return epoch;
}
bool CorrectionSourceCurrent(const std::size_t source, const std::uint64_t incarnation)
{
  if (source >= desired.size())
    return false;
  portENTER_CRITICAL(&guard);
  const bool current = desired[source].enabled && desired[source].epoch == incarnation;
  portEXIT_CRITICAL(&guard);
  return current;
}
bool SubmitCorrectionBytes(const std::size_t source,
                           const std::uint64_t incarnation,
                           const std::uint8_t* bytes,
                           const std::size_t size,
                           const std::int64_t receipt_ns,
                           const bool complete_frame)
{
  if (!ingress || !bytes || !size || size > Gateway::kMaxFrame || receipt_ns < 0 ||
      !CorrectionSourceCurrent(source, incarnation))
    return false;
  Input message;
  std::memcpy(message.bytes.data(), bytes, size);
  message.size = size;
  message.source = source;
  message.incarnation = incarnation;
  message.receipt_ns = receipt_ns;
  message.complete = complete_frame;
  if (xQueueSend(ingress, &message, 0) == pdTRUE)
    return true;
  portENTER_CRITICAL(&guard);
  ++stats.ingress_dropped;
  portEXIT_CRITICAL(&guard);
  return false;
}
bool SetCorrectionDestination(const std::size_t sink, FrameDestination callback, void* context)
{
  if (sink >= destinations.size())
    return false;
  portENTER_CRITICAL(&guard);
  const auto ticket = destinations[sink].Publish(callback, context);
  portEXIT_CRITICAL(&guard);
  return ticket != 0;
}
std::uint64_t WithdrawCorrectionDestination(const std::size_t sink)
{
  if (sink >= destinations.size())
    return 0;
  portENTER_CRITICAL(&guard);
  const auto ticket = destinations[sink].Publish(nullptr, nullptr);
  portEXIT_CRITICAL(&guard);
  return ticket;
}
bool CorrectionDestinationRetired(const std::size_t sink, const std::uint64_t ticket)
{
  if (sink >= destinations.size())
    return false;
  portENTER_CRITICAL(&guard);
  const bool retired = destinations[sink].Applied(ticket);
  portEXIT_CRITICAL(&guard);
  return retired;
}
bool RetireCorrectionDestination(const std::size_t sink, std::uint32_t timeout_ms)
{
  const auto ticket = WithdrawCorrectionDestination(sink);
  if (!ticket)
    return false;
  const auto started = esp_timer_get_time();
  for (;;)
  {
    if (CorrectionDestinationRetired(sink, ticket))
      return true;
    if (esp_timer_get_time() - started >= static_cast<std::int64_t>(timeout_ms) * 1000)
      return false;  // Caller MUST retain its context; timeout is not retirement.
    vTaskDelay(FiniteWaitTicks(1, configTICK_RATE_HZ));
  }
}

GatewayServiceStats GatewayStats()
{
  portENTER_CRITICAL(&guard);
  const auto result = stats;
  portEXIT_CRITICAL(&guard);
  return result;
}
}  // namespace ug::embedded
