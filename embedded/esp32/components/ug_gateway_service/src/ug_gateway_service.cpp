// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_gateway_service.hpp"
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
struct Destination
{
  FrameDestination callback{};
  void* context{};
  std::uint64_t revision{};
};
Gateway gateway;
Gateway::Frame frame;
std::array<Control, Gateway::kSources> desired{}, applied{};
std::array<Destination, Gateway::kSinks> destinations{}, active_destinations{};
GatewayServiceStats stats{};
portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t ingress;
StaticQueue_t ingress_control;
constexpr std::size_t kIngressDepth = 8;
std::array<std::uint8_t, kIngressDepth * sizeof(Input)> ingress_memory{};
Input input;
void RefreshControls()
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
void RefreshDestinations()
{
  for (std::size_t i = 0; i < destinations.size(); ++i)
  {
    portENTER_CRITICAL(&guard);
    const auto target = destinations[i];
    portEXIT_CRITICAL(&guard);
    if (target.revision != active_destinations[i].revision)
    {
      gateway.ConfigureSink(i, target.callback != nullptr, 3000000000LL);
      active_destinations[i] = target;
    }
  }
}
void Owner(void*)
{
  WorkerHealth health(ServiceId::kRouting, 5000000000LL, true);
  for (;;)
  {
    RefreshControls();
    RefreshDestinations();
    if (xQueueReceive(ingress, &input, FiniteWaitTicks(5, configTICK_RATE_HZ)) == pdTRUE)
    {
      RefreshControls();
      if (input.complete)
        gateway.FeedFrame(
            input.source, input.incarnation, input.bytes.data(), input.size, input.receipt_ns);
      else
        gateway.Feed(
            input.source, input.incarnation, input.bytes.data(), input.size, input.receipt_ns);
    }
    RefreshControls();
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
  ingress =
      xQueueCreateStatic(kIngressDepth, sizeof(Input), ingress_memory.data(), &ingress_control);
  if (!ingress)
    return ESP_ERR_NO_MEM;

  ExpectWorker(ServiceId::kRouting, 5000000000LL);
  if (xTaskCreate(Owner, "ug_rtcm", 6144, nullptr, 5, nullptr) != pdPASS)
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
  if (destinations[sink].revision == UINT64_MAX)
  {
    portEXIT_CRITICAL(&guard);
    return false;
  }
  const auto revision = destinations[sink].revision + 1;
  destinations[sink] = {callback, context, revision};
  portEXIT_CRITICAL(&guard);
  return true;
}
GatewayServiceStats GatewayStats()
{
  portENTER_CRITICAL(&guard);
  const auto result = stats;
  portEXIT_CRITICAL(&guard);
  return result;
}
}  // namespace ug::embedded
