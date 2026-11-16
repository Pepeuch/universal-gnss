// SPDX-License-Identifier: LGPL-3.0-only
// Compile the actual service, including its private storage/startup boundary.
#include "../../components/ug_gateway_service/src/ug_gateway_service.cpp"
#include "esp_heap_caps.h"
#include "ug_rtcm.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <new>
#include <string>
#include <vector>

namespace
{
unsigned critical_depth{}, allocations{}, frees{}, queue_creates{}, task_creates{}, queue_deletes{};
std::size_t allocated_bytes{}, cpp_allocations{};
void* allocated{};
bool refuse_memory{}, wrong_region{}, refuse_queue{}, refuse_task{};
TaskFunction_t worker{};
std::int64_t now_us = 10000000;
unsigned remaining{};
std::function<void()> progress_hook;
struct WorkerStopped
{
};
std::array<std::vector<ug::embedded::Gateway::Frame>, 7> received;
bool reject_destination{};
bool benchmark_mode{};
std::uint64_t benchmark_frames{}, benchmark_bytes{};
bool Save(const ug::embedded::Gateway::Frame& frame, void* context)
{
  assert(critical_depth == 0);
  const auto index = static_cast<std::size_t*>(context);
  if (benchmark_mode)
  {
    ++benchmark_frames;
    benchmark_bytes += frame.size;
    return true;
  }
  if (reject_destination && *index == 6)
    return false;
  received[*index].push_back(frame);  // Callback copies; never keeps a borrowed pointer.
  return true;
}
void Run(unsigned loops)
{
  remaining = loops;
  try
  {
    assert(worker);
    worker(nullptr);
    assert(false);
  }
  catch (const WorkerStopped&)
  {
    assert(remaining == 0 && critical_depth == 0);
  }
}
std::vector<std::uint8_t> MakeFrame(std::size_t payload = 32)
{
  std::vector<std::uint8_t> bytes(payload + 6, 0);
  bytes[0] = 0xd3;
  bytes[1] = static_cast<std::uint8_t>(payload >> 8);
  bytes[2] = static_cast<std::uint8_t>(payload);
  std::uint32_t crc{};
  assert(ug_rtcm_crc24q(bytes.data(), bytes.size() - 3, &crc));
  bytes[bytes.size() - 3] = crc >> 16;
  bytes[bytes.size() - 2] = crc >> 8;
  bytes.back() = crc;
  return bytes;
}
void Cleanup()
{
  // Test process only: all simulated tasks are stopped. Firmware never releases
  // this boot-owned block or any pointer already published to a queue.
#if CONFIG_IDF_TARGET_ESP32S3
  if (auto* value = ug::embedded::routing_payload.Get())
  {
    value->~RoutingPayload();
    heap_caps_free(value);
  }
#endif
}
}  // namespace

void* operator new(std::size_t bytes)
{
  ++cpp_allocations;
  if (auto* memory = std::malloc(bytes ? bytes : 1))
    return memory;
  throw std::bad_alloc();
}
void operator delete(void* memory) noexcept
{
  std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
  std::free(memory);
}
void RoutingTestEnter(portMUX_TYPE*)
{
  ++critical_depth;
}
void RoutingTestExit(portMUX_TYPE*)
{
  assert(critical_depth);
  --critical_depth;
}
void* heap_caps_malloc(std::size_t bytes, unsigned caps)
{
  assert(critical_depth == 0 && caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ++allocations;
  allocated_bytes = bytes;
  if (refuse_memory)
    return nullptr;
  allocated = std::malloc(bytes);
  assert(allocated);
  std::memset(allocated, 0xa5, bytes);
  return allocated;
}
void heap_caps_free(void* memory)
{
  assert(memory == allocated && critical_depth == 0);
  ++frees;
  std::free(memory);
  allocated = nullptr;
}
bool esp_ptr_external_ram(const void* memory)
{
  return memory == allocated && !wrong_region;
}
std::int64_t esp_timer_get_time()
{
  return now_us;
}
QueueHandle_t xQueueCreateStatic(std::size_t depth,
                                 std::size_t width,
                                 std::uint8_t* memory,
                                 StaticQueue_t* control)
{
  ++queue_creates;
  assert(depth == 8 && width <= 1100 && memory && control && !critical_depth);
#if CONFIG_IDF_TARGET_ESP32S3
  // The payload is external, but FreeRTOS control must be outside that block.
  const auto begin = reinterpret_cast<std::uintptr_t>(allocated);
  const auto end = begin + allocated_bytes;
  const auto data = reinterpret_cast<std::uintptr_t>(memory);
  const auto state = reinterpret_cast<std::uintptr_t>(control);
  assert(data >= begin && data + depth * width <= end);
  assert(state < begin || state >= end);
#endif
  if (refuse_queue)
    return nullptr;
  *control = {memory, depth, width, 0, 0, true};
  return control;
}
int xQueueSend(QueueHandle_t queue, const void* input, TickType_t wait)
{
  assert(queue && queue->live && input && wait == 0);
  if (queue->count == queue->depth)
    return pdFALSE;
  std::memcpy(queue->data + ((queue->head + queue->count++) % queue->depth) * queue->width,
              input,
              queue->width);
  return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue, void* output, TickType_t wait)
{
  assert(queue && queue->live && output && wait > 0);
  if (!queue->count)
    return pdFALSE;
  std::memcpy(output, queue->data + queue->head * queue->width, queue->width);
  queue->head = (queue->head + 1) % queue->depth;
  --queue->count;
  return pdTRUE;
}
void vQueueDelete(QueueHandle_t queue)
{
  assert(queue && queue->live);
  ++queue_deletes;
  queue->live = false;
}
int xTaskCreate(
    TaskFunction_t entry, const char* name, unsigned stack, void*, unsigned priority, void*)
{
  ++task_creates;
  assert(stack == 6144 && priority == 5 && !std::strcmp(name, "ug_rtcm"));
  assert(!critical_depth);
  if (refuse_task)
    return pdFALSE;
  worker = entry;
  return pdPASS;
}
extern "C" void espnow_rtcm_invalidate_pending()
{
  assert(!critical_depth);
}
namespace ug::embedded
{
void ExpectWorker(ServiceId id, std::int64_t deadline)
{
  assert(id == ServiceId::kRouting && deadline == 5000000000LL);
}
WorkerHealth::WorkerHealth(ServiceId, std::int64_t, bool)
{
}
WorkerHealth::~WorkerHealth() = default;
void WorkerHealth::Progress()
{
  if (progress_hook)
    progress_hook();
  if (--remaining == 0)
    throw WorkerStopped{};
}
}  // namespace ug::embedded

int main(int argc, char** argv)
{
  using namespace ug::embedded;
  const std::string scenario = argc > 1 ? argv[1] : "normal";
  refuse_memory = scenario == "no-memory";
  wrong_region = scenario == "wrong-region";
  refuse_queue = scenario == "queue-failure";
  refuse_task = scenario == "task-failure";
  const auto before = cpp_allocations;
  const auto result = StartGatewayService();
  // Gateway/framer default construction may not hide another heap allocation.
  assert(cpp_allocations == before);
#if CONFIG_IDF_TARGET_ESP32S3
  assert(allocations == 1 && allocated_bytes > 25000);
  if (refuse_memory || wrong_region)
  {
    assert(result == ESP_ERR_NO_MEM && !queue_creates && !task_creates && !worker);
    refuse_memory = wrong_region = false;
    assert(StartGatewayService() == ESP_ERR_NO_MEM && allocations == 1);
    const auto bytes = MakeFrame();
    assert(!SubmitCorrectionBytes(0, 1, bytes.data(), bytes.size(), 0, true));
    assert(!allocated && frees == (scenario == "wrong-region" ? 1u : 0u));
    return 0;
  }
#else
  assert(!allocations);
#endif
  if (refuse_queue || refuse_task)
  {
    assert(result == ESP_ERR_NO_MEM && !worker);
    assert(queue_deletes == (refuse_task ? 1u : 0u));
    auto* stable = allocated;
    refuse_queue = refuse_task = false;
    assert(StartGatewayService() == ESP_OK && allocated == stable);
  }
  else
    assert(result == ESP_OK);
  assert(StartGatewayService() == ESP_ERR_INVALID_STATE);
  auto epoch = BeginCorrectionSource(0, true);
  assert(epoch && CorrectionSourceCurrent(0, epoch));
  std::array<std::size_t, 7> slots{};
  for (std::size_t i = 0; i < slots.size(); ++i)
  {
    slots[i] = i;
    assert(SetCorrectionDestination(i, Save, &slots[i]));
  }
  if (scenario == "benchmark")
  {
    benchmark_mode = true;
    for (const std::size_t payload : {0u, 32u, 1023u})
    {
      const auto bytes = MakeFrame(payload);
      std::vector<std::int64_t> batches;
      batches.reserve(2000);
      benchmark_frames = benchmark_bytes = 0;
      const auto cpu_start = std::clock();
      const auto started = std::chrono::steady_clock::now();
      for (unsigned batch = 0; batch < 2000; ++batch)
      {
        const auto before = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 8; ++i)
          assert(SubmitCorrectionBytes(0, epoch, bytes.data(), bytes.size(), now_us * 1000, true));
        Run(8);
        batches.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - before)
                              .count());
      }
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
      const auto cpu_ns = (std::clock() - cpu_start) * 1000000000LL / CLOCKS_PER_SEC;
      std::sort(batches.begin(), batches.end());
      assert(benchmark_frames == 16000u * 7 && benchmark_bytes == benchmark_frames * bytes.size());
      std::printf("ROUTING_BENCH frame_bytes=%zu input_frames=16000 outputs=%llu bytes=%llu "
                  "wall_ns=%lld cpu_ns=%lld batch_p50_ns=%lld batch_p95_ns=%lld\n",
                  bytes.size(),
                  static_cast<unsigned long long>(benchmark_frames),
                  static_cast<unsigned long long>(benchmark_bytes),
                  static_cast<long long>(elapsed),
                  static_cast<long long>(cpu_ns),
                  static_cast<long long>(batches[batches.size() / 2]),
                  static_cast<long long>(batches[batches.size() * 95 / 100]));
    }
    Cleanup();
    return 0;
  }
  const auto bytes = MakeFrame(1023);
  reject_destination = true;
  for (unsigned i = 0; i < 8; ++i)
    assert(SubmitCorrectionBytes(0, epoch, bytes.data(), bytes.size(), 10000000000LL + i, true));
  assert(!SubmitCorrectionBytes(0, epoch, bytes.data(), bytes.size(), 10000000000LL, true));
  now_us += 100;
  Run(8);
  for (std::size_t i = 0; i < slots.size(); ++i)
  {
    assert(received[i].size() == (i == 6 ? 0u : 8u));
    for (std::size_t j = 0; j < received[i].size(); ++j)
    {
      const auto& f = received[i][j];
      assert(f.source == 0 && f.incarnation == epoch && f.receipt_ns == 10000000000LL + j);
      assert(f.size == bytes.size() && std::equal(bytes.begin(), bytes.end(), f.bytes.begin()));
    }
  }
  auto metrics = GatewayStats();
  assert(metrics.routing.valid == 8 && metrics.ingress_dropped == 1 &&
         metrics.destination_dropped == 8);
  // Queued old-incarnation data is rejected, without aliasing the new arrival.
  assert(SubmitCorrectionBytes(0, epoch, bytes.data(), bytes.size(), now_us * 1000, true));
  epoch = BeginCorrectionSource(0, true);
  Run(1);
  assert(GatewayStats().routing.valid == 8);
  auto other = BeginCorrectionSource(1, true);
  assert(SubmitCorrectionBytes(1, other, bytes.data(), bytes.size(), 1, true));
  Run(1);
  assert(GatewayStats().routing.dropped_stale == 7);
  auto corrupt = bytes;
  corrupt.back() ^= 1;
  assert(SubmitCorrectionBytes(0, epoch, corrupt.data(), corrupt.size(), now_us * 1000, true));
  Run(1);
  assert(GatewayStats().routing.rejected == 1);
  // A source switch arriving during processing invalidates the next ingress item.
  assert(SubmitCorrectionBytes(0, epoch, bytes.data(), bytes.size(), now_us * 1000, true));
  assert(SubmitCorrectionBytes(0, epoch, bytes.data(), bytes.size(), now_us * 1000, true));
  progress_hook = [&] { BeginCorrectionSource(0, true); };
  Run(2);
  progress_hook = {};
  assert(GatewayStats().routing.valid == 10);
  // The incremental byte path keeps the FIRST fragment's monotonic timestamp.
  // Complete-frame ingestion above does not exercise the framer's retained data.
  const auto fragmented = BeginCorrectionSource(2, true);
  assert(SubmitCorrectionBytes(2, fragmented, bytes.data(), 400, now_us * 1000, false));
  assert(SubmitCorrectionBytes(
      2, fragmented, bytes.data() + 400, bytes.size() - 400, now_us * 1000 + 10, false));
  Run(2);
  assert(GatewayStats().routing.valid == 11);
  assert(received[0].back().source == 2 && received[0].back().receipt_ns == now_us * 1000);
  // A reconnect must not join an old partial frame to a new incarnation.
  assert(SubmitCorrectionBytes(2, fragmented, bytes.data(), 400, now_us * 1000 + 20, false));
  Run(1);
  const auto replacement = BeginCorrectionSource(2, true);
  assert(SubmitCorrectionBytes(
      2, replacement, bytes.data() + 400, bytes.size() - 400, now_us * 1000 + 30, false));
  Run(1);
  assert(GatewayStats().routing.valid == 11);
#if CONFIG_IDF_TARGET_ESP32S3
  assert(allocations == 1);  // No runtime reallocation or hidden SRAM fallback.
#endif
  std::printf("ROUTING_TEST storage=%zu frame=%zu mode=%s PASS\n",
              allocated_bytes,
              sizeof(Gateway::Frame),
              scenario.c_str());
  Cleanup();
}
