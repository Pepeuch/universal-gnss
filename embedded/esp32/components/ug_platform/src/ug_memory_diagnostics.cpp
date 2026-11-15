// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_memory_diagnostics.hpp"
#include "sdkconfig.h"
#include "ug_json_stress.hpp"
#include "ug_s3_json_memory.hpp"
#include "ug_s3_memory_options.hpp"
#if CONFIG_IDF_TARGET_ESP32S3
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_heap_trace.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
namespace ug::embedded
{
namespace
{
constexpr std::array<const char*, 8> names{
    "resources", "status", "gnss", "satellites", "config", "ntrip", "events", "other"};
struct Metric
{
  std::uint32_t requests{};
  std::uint64_t last_us{}, max_us{};
  std::size_t internal_low{}, psram_low{};
};
std::array<Metric, names.size()> metrics{};
std::mutex metrics_guard;
std::atomic<TaskHandle_t> web_task{};
std::size_t active_route;
std::uint32_t request_json_failures;
std::atomic<std::uint32_t> failures{};
std::mutex trace_guard;
bool tracing{};
heap_trace_record_t* trace_records;
esp_timer_handle_t trace_timer;
constexpr std::size_t kTraceCount = 1024;
void FailedAllocation(std::size_t, std::uint32_t, const char*)
{
  failures.fetch_add(1, std::memory_order_relaxed);
}
void StopTraceLocked()
{
  if (tracing)
  {
    heap_trace_stop();
    tracing = false;
  }
}
void StopTrace(void*)
{
  std::lock_guard<std::mutex> lock(trace_guard);
  StopTraceLocked();
}
void PrintHeap(const char* name, std::uint32_t caps)
{
  multi_heap_info_t info{};
  heap_caps_get_info(&info, caps);
  std::printf("UG_MEM heap=%s free=%zu min=%zu largest=%zu allocated=%zu allocated_blocks=%zu "
              "free_blocks=%zu\n",
              name,
              info.total_free_bytes,
              info.minimum_free_bytes,
              info.largest_free_block,
              info.total_allocated_bytes,
              info.allocated_blocks,
              info.free_blocks);
}
std::size_t Route(std::string_view uri)
{
  if (uri == "/api/resources")
    return 0;
  if (uri == "/api/status" || uri == "/status")
    return 1;
  if (uri == "/api/gnss/satellites")
    return 3;
  if (uri.substr(0, 10) == "/api/gnss/")
    return 2;
  if (uri.substr(0, 11) == "/api/config")
    return 4;
  if (uri.substr(0, 11) == "/api/ntrip/")
    return 5;
  if (uri == "/api/diagnostics/events")
    return 6;
  return 7;
}
}  // namespace
void InitializeMemoryDiagnostics()
{
  InitializeS3JsonMemory();
  std::printf(
      "UG_MEM mode console_psram=%d json_psram=%d\n", UG_S3_CONSOLE_PSRAM, UG_S3_JSON_PSRAM);
  const auto registered = heap_caps_register_failed_alloc_callback(FailedAllocation);
  esp_timer_create_args_t args{};
  args.callback = StopTrace;
  args.name = "ug_mem_trace";
  const auto timer = esp_timer_create(&args, &trace_timer);
  std::printf("UG_MEM init failures_hook=%d timer=%d trace_window_us=5000000\n", registered, timer);
}
void PrintMemoryDiagnostics()
{
  const auto begin = esp_timer_get_time();
  const auto* app = esp_app_get_description();
  std::printf("UG_MEM uptime_us=%lld failed_allocations=%lu elf=",
              static_cast<long long>(begin),
              static_cast<unsigned long>(failures.load()));
  for (std::size_t i = 0; i < 8; ++i)
    std::printf("%02x", app->app_elf_sha256[i]);
  std::puts("");
  std::printf("UG_MEM json_failures=%lu injected=%lu\n",
              static_cast<unsigned long>(S3JsonMemoryFailures()),
              static_cast<unsigned long>(S3JsonInjectedFailures()));
  PrintHeap("internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  PrintHeap("dma", MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  PrintHeap("psram", MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  // External scratch is diagnostic-only. Stacks and DMA never move here.
  constexpr std::size_t capacity = 48;
  auto* tasks = static_cast<TaskStatus_t*>(
      heap_caps_calloc(capacity, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (tasks)
  {
    configRUN_TIME_COUNTER_TYPE total{};
    const auto count = uxTaskGetSystemState(tasks, capacity, &total);
    std::printf("UG_MEM tasks=%u capacity=%u runtime_us=%llu scratch_bytes=%zu\n",
                count,
                unsigned(capacity),
                static_cast<unsigned long long>(total),
                capacity * sizeof(TaskStatus_t));
    for (UBaseType_t i = 0; i < count; ++i)
    {
      const auto& task = tasks[i];
      // Do not dereference borrowed TCB name pointers after the snapshot.
      const char* owner = task.xHandle == xTaskGetIdleTaskHandleForCore(0)   ? "idle0"
                          : task.xHandle == xTaskGetIdleTaskHandleForCore(1) ? "idle1"
                          : task.xHandle == xTaskGetCurrentTaskHandle()      ? "console"
                          : task.xHandle == web_task                         ? "https"
                                                                             : "task";
      std::printf(
          "UG_MEM task=%lu owner=%s priority=%u state=%u stack_min_bytes=%lu runtime_us=%llu\n",
          static_cast<unsigned long>(task.xTaskNumber),
          owner,
          unsigned(task.uxCurrentPriority),
          unsigned(task.eCurrentState),
          static_cast<unsigned long>(task.usStackHighWaterMark),
          static_cast<unsigned long long>(task.ulRunTimeCounter));
    }
    heap_caps_free(tasks);
  }
  else
    std::puts("UG_MEM tasks_unavailable=no_external_scratch");
  std::array<Metric, names.size()> copy;
  {
    std::lock_guard<std::mutex> lock(metrics_guard);
    copy = metrics;
  }
  for (std::size_t i = 0; i < copy.size(); ++i)
  {
    const auto& m = copy[i];
    std::printf("UG_MEM api=%s requests=%lu last_us=%llu max_us=%llu sampled_internal_low=%zu "
                "sampled_psram_low=%zu\n",
                names[i],
                static_cast<unsigned long>(m.requests),
                static_cast<unsigned long long>(m.last_us),
                static_cast<unsigned long long>(m.max_us),
                m.internal_low,
                m.psram_low);
  }
  std::printf("UG_MEM diagnostic_elapsed_us=%lld\n",
              static_cast<long long>(esp_timer_get_time() - begin));
}
bool MemoryTraceCommand(bool start, std::string& error)
{
  std::lock_guard<std::mutex> lock(trace_guard);
  if (!trace_timer)
  {
    error = "memory trace timer unavailable";
    return false;
  }
  if (start)
  {
    if (tracing)
    {
      error = "memory trace already active";
      return false;
    }
    if (!trace_records)
      trace_records = static_cast<heap_trace_record_t*>(heap_caps_calloc(
          kTraceCount, sizeof(heap_trace_record_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!trace_records)
    {
      error = "memory trace external allocation failed";
      return false;
    }
    auto code = heap_trace_init_standalone(trace_records, kTraceCount);
    if (code == ESP_OK)
      code = heap_trace_start(HEAP_TRACE_ALL);
    if (code != ESP_OK)
    {
      error = "memory trace initialization failed";
      return false;
    }
    tracing = true;
    code = esp_timer_start_once(trace_timer, 5000000);
    if (code != ESP_OK)
    {
      StopTraceLocked();
      error = "memory trace deadline failed";
      return false;
    }
    std::printf("UG_MEM trace_started capacity=%zu bytes=%zu window_us=5000000 external_records=1 "
                "isr_allocations_excluded=1\n",
                kTraceCount,
                kTraceCount * sizeof(heap_trace_record_t));
    return true;
  }
  esp_timer_stop(trace_timer);
  StopTraceLocked();
  heap_trace_summary_t summary{};
  if (!trace_records || heap_trace_summary(&summary) != ESP_OK)
  {
    error = "no memory trace";
    return false;
  }
  std::printf("UG_MEM trace_records=%zu allocations=%zu frees=%zu overflow=%zu capacity=%zu\n",
              summary.count,
              summary.total_allocations,
              summary.total_frees,
              summary.has_overflowed,
              summary.capacity);
  for (std::size_t i = 0; i < summary.count; ++i)
  {
    heap_trace_record_t record{};
    if (heap_trace_get(i, &record) != ESP_OK)
      break;
    std::printf("UG_MEM allocation=%zu address=%p size=%zu external=%d freed=%d",
                i,
                record.address,
                record.size,
                esp_ptr_external_ram(record.address),
                record.freed);
    for (auto caller : record.alloced_by)
      std::printf(" caller=%p", caller);
    std::puts("");
  }
  heap_trace_init_standalone(nullptr, 0);
  heap_caps_free(trace_records);
  trace_records = nullptr;
  return true;
}
void SampleWebMemory()
{
  if (xTaskGetCurrentTaskHandle() != web_task)
    return;
  const auto internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const auto external = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  std::lock_guard<std::mutex> lock(metrics_guard);
  auto& m = metrics[active_route];
  m.internal_low = m.internal_low ? std::min(m.internal_low, internal) : internal;
  m.psram_low = m.psram_low ? std::min(m.psram_low, external) : external;
}
bool WebJsonMemoryHealthy()
{
  return xTaskGetCurrentTaskHandle() == web_task && request_json_failures == S3JsonMemoryFailures();
}
bool RunMemoryJsonStress(std::string& error)
{
#if UG_S3_JSON_PSRAM
  for (unsigned count : {0U, 16U, 64U})
  {
    const auto failures_before = S3JsonMemoryFailures();
    auto root = BuildMemoryJsonFixture(count);
    char* text = root ? cJSON_PrintUnformatted(root.get()) : nullptr;
    const bool ok = text && S3JsonMemoryFailures() == failures_before;
    const auto length = text ? std::strlen(text) : 0;
    cJSON_free(text);
    if (!ok || length > 32768)
    {
      error = "synthetic JSON allocation/size failure";
      return false;
    }
    std::printf("UG_MEM synthetic_json rows=%u bytes=%zu no_receiver_data=1\n", count, length);
  }
  return true;
#else
  error = "synthetic JSON test requires S3 JSON PSRAM mode";
  return false;
#endif
}
WebMemoryScope::WebMemoryScope(std::string_view uri)
    : started_(esp_timer_get_time()), route_(Route(uri))
{
  web_task = xTaskGetCurrentTaskHandle();
  active_route = route_;
  request_json_failures = S3JsonMemoryFailures();
  SampleWebMemory();
}
WebMemoryScope::~WebMemoryScope()
{
  SampleWebMemory();
  const auto elapsed = static_cast<std::uint64_t>(esp_timer_get_time() - started_);
  std::lock_guard<std::mutex> lock(metrics_guard);
  auto& m = metrics[route_];
  ++m.requests;
  m.last_us = elapsed;
  m.max_us = std::max(m.max_us, elapsed);
}
}  // namespace ug::embedded
#else
namespace ug::embedded
{
void InitializeMemoryDiagnostics()
{
}
void PrintMemoryDiagnostics()
{
}
bool MemoryTraceCommand(bool, std::string& error)
{
  error = "S3 diagnostics only";
  return false;
}
void SampleWebMemory()
{
}
bool WebJsonMemoryHealthy()
{
  return true;
}
bool RunMemoryJsonStress(std::string& error)
{
  error = "S3 only";
  return false;
}
WebMemoryScope::WebMemoryScope(std::string_view)
{
}
WebMemoryScope::~WebMemoryScope()
{
}
}  // namespace ug::embedded
#endif
