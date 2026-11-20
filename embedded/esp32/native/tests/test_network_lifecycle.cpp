// SPDX-License-Identifier: LGPL-3.0-only
#ifndef UG_NETWORK_COOPERATIVE
#define UG_NETWORK_COOPERATIVE 0
#endif
#include "../../components/ug_services/src/ug_network_services.cpp"
#include "ug_rtcm.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
using namespace ug::embedded;
namespace
{
struct Task
{
  TaskFunction_t fn;
  void* context;
  unsigned stack;
  std::string name;
};
struct Yield
{
};
struct Exit
{
};
std::vector<Task> tasks;
auto config = std::make_shared<Settings>();
Capabilities caps{true, true, false, 5};
bool no_memory{}, no_task{}, no_queue{};
unsigned finished{}, allocations{}, retire_calls{}, refuse_retire{}, critical_depth{};
std::int64_t clock_us = 10000000;
std::vector<void*> payloads;
void* expect_zeroed_payload{};
std::size_t expect_zeroed_size{};
bool payload_wipe_verified{};
std::function<void()> on_delay;
bool inject_caster_frame{}, throw_progress{};
int last_service_error{};
void TickManager()
{
  try
  {
    tasks[0].fn(tasks[0].context);
  }
  catch (const Yield&)
  {
  }
}
void StopWorker(Task task)
{
  try
  {
    task.fn(task.context);
    assert(false);
  }
  catch (const Exit&)
  {
  }
}
}  // namespace
std::int64_t esp_timer_get_time()
{
  return clock_us;
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
void* heap_caps_malloc(std::size_t size, unsigned flags)
{
  assert(flags == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ++allocations;
  if (no_memory)
    return nullptr;
  auto* p = std::malloc(size);
  payloads.push_back(p);
  return p;
}
void heap_caps_free(void* p)
{
  if (p == expect_zeroed_payload)
  {
    const auto* bytes = static_cast<const unsigned char*>(p);
    assert(std::all_of(bytes, bytes + expect_zeroed_size, [](unsigned char b) { return b == 0; }));
    payload_wipe_verified = true;
    expect_zeroed_payload = nullptr;
  }
  payloads.erase(std::remove(payloads.begin(), payloads.end(), p), payloads.end());
  std::free(p);
}
QueueHandle_t
xQueueCreateStatic(std::size_t d, std::size_t w, std::uint8_t* bytes, StaticQueue_t* control)
{
  if (no_queue)
    return nullptr;
  *control = {bytes, d, w, 0, 0, true};
  return control;
}
int xQueueSend(QueueHandle_t q, const void* bytes, TickType_t)
{
  assert(q && q->live);
  if (q->count == q->depth)
    return pdFALSE;
  std::memcpy(q->data + ((q->head + q->count) % q->depth) * q->width, bytes, q->width);
  ++q->count;
  return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void* bytes, TickType_t)
{
  assert(q && q->live);
  if (!q->count)
    return pdFALSE;
  std::memcpy(bytes, q->data + q->head * q->width, q->width);
  q->head = (q->head + 1) % q->depth;
  --q->count;
  return pdTRUE;
}
int xQueueReset(QueueHandle_t q)
{
  assert(q && q->live);
  q->count = 0;
  return pdTRUE;
}
int xTaskCreate(TaskFunction_t fn, const char* name, unsigned stack, void* ctx, unsigned, void*)
{
  if (no_task)
    return pdFALSE;
  tasks.push_back({fn, ctx, stack, name});
  return pdPASS;
}
void vTaskDelay(TickType_t ticks)
{
  assert(!critical_depth);
  clock_us += ticks * 1000000 / configTICK_RATE_HZ;
  if (on_delay)
    on_delay();
  else
    throw Yield{};
}
void vTaskDelete(void* p)
{
  assert(!p && !critical_depth);
  throw Exit{};
}
namespace ug::embedded
{
bool InitializeNetworkAllocation()
{
  return true;
}
std::shared_ptr<const Settings> Configuration()
{
  return config;
}
const Capabilities& BoardCapabilities()
{
  return caps;
}
void ExpectWorker(ServiceId, std::int64_t)
{
}
void ObserveService(ServiceId, ServiceState, std::int32_t error)
{
  last_service_error = error;
}
WorkerHealth::WorkerHealth(ServiceId, std::int64_t, bool)
{
}
WorkerHealth::~WorkerHealth() = default;
void WorkerHealth::Progress()
{
  if (throw_progress)
    throw std::bad_alloc{};
}
void WorkerHealth::Finish()
{
  ++finished;
}
std::uint64_t BeginCorrectionSource(std::size_t, bool)
{
  return 1;
}
bool CorrectionSourceCurrent(std::size_t, std::uint64_t)
{
  return true;
}
bool SubmitCorrectionBytes(
    std::size_t, std::uint64_t, const std::uint8_t*, std::size_t, std::int64_t, bool)
{
  return true;
}
bool SetCorrectionDestination(std::size_t sink, FrameDestination callback, void* context)
{
  if (sink == 6 && callback && inject_caster_frame)
  {
    Gateway::Frame frame{};
    frame.size = 8;
    frame.bytes[0] = 0xd3;
    frame.bytes[2] = 2;
    frame.bytes[3] = 0x3e;
    frame.bytes[4] = 0xd0;
    std::uint32_t crc{};
    assert(ug_rtcm_crc24q(frame.bytes.data(), 5, &crc));
    frame.bytes[5] = crc >> 16;
    frame.bytes[6] = crc >> 8;
    frame.bytes[7] = crc;
    assert(ug_rtcm_frame_valid(frame.bytes.data(), frame.size));
    frame.receipt_ns = Now();
    frame.incarnation = 1;
    assert(callback(frame, context));
  }
  return true;
}
bool RetireCorrectionDestination(std::size_t, std::uint32_t)
{
  ++retire_calls;
  return retire_calls > refuse_retire;
}
std::uint64_t WithdrawCorrectionDestination(std::size_t)
{
  return ++retire_calls;
}
bool CorrectionDestinationRetired(std::size_t, std::uint64_t ticket)
{
  return ticket && ++retire_calls > refuse_retire;
}
}  // namespace ug::embedded
#ifndef UG_NETWORK_TEST_LIBRARY
int main()
{
  // Disabled endpoints allocate no queues/owners or workers. Repeated start is refused.
  assert(StartNetworkServices() == ESP_OK && tasks.size() == 1);
  assert(StartNetworkServices() == ESP_ERR_INVALID_STATE);
  NetworkAvailabilityChanged(true);
  TickManager();
  assert(tasks.size() == 1 && allocations == 0);
  config->publishers[0].enabled = true;
  no_memory = true;
  TickManager();
  assert(tasks.size() == 1 && !network_workers[1].live);
  no_memory = false;
  clock_us += 2000000;
  no_queue = true;
  TickManager();
  assert(tasks.size() == 1 && !network_workers[1].live);
  no_queue = false;
  clock_us += 2000000;
  no_task = true;
  TickManager();
  assert(!network_workers[1].live);
  no_task = false;
  clock_us += 2000000;
  TickManager();
  assert(tasks.size() == 2 && network_workers[1].live);
  auto* retained = publisher_owners[0];
  assert(retained && publishers[0].queue);
  const auto reserved = allocations;
  TickManager();
  assert(tasks.size() == 2 && allocations == reserved);  // no duplicate owner
  config->publishers[0].enabled = false;
  refuse_retire = 2;  // Keep the context alive across two failed retirement waits.
  StopWorker(tasks[1]);
  assert(!network_workers[1].live && finished == 1 && retire_calls == 3);
  // Repeated activation retains logical filter/metric owner and payload capacity.
  for (unsigned n = 0; n < 100; ++n)
  {
    config->publishers[0].enabled = true;
    TickManager();
    auto task = tasks.back();
    assert(network_workers[1].live && publisher_owners[0] == retained && allocations == reserved);
    config->publishers[0].enabled = false;
    StopWorker(task);
    assert(!network_workers[1].live);
  }
  // A nested C++ constructor failure releases its explicit external block.
  struct Throwing
  {
    Throwing()
    {
      throw std::bad_alloc{};
    }
  };
  Throwing* bad_owner = nullptr;
  const auto held = payloads.size();
  assert(!ReserveOwner(bad_owner, [](void* memory) { return new (memory) Throwing{}; }));
  assert(!bad_owner && payloads.size() == held);
  struct OtherException
  {
    OtherException()
    {
      throw 17;
    }
  };
  OtherException* unexpected = nullptr;
  bool caught = false;
  try
  {
    (void)ReserveOwner(unexpected, [](void* memory) { return new (memory) OtherException{}; });
  }
  catch (int value)
  {
    caught = value == 17;
  }
  assert(caught && !unexpected && payloads.size() == held);
  // An allocation failure inside an executing owner unwinds its task context,
  // clears borrowed trace pointers, closes/withdraws and backs off before retry.
  config->publishers[0].enabled = true;
  TickManager();
  throw_progress = true;
  StopWorker(tasks.back());
  throw_progress = false;
  assert(!network_workers[1].live && network_workers[1].failed &&
         last_service_error == ESP_ERR_NO_MEM);
  const auto task_count = tasks.size();
  TickManager();
  assert(tasks.size() == task_count);
  clock_us += 2000000;
  TickManager();
  assert(tasks.size() == task_count + 1);
  config->publishers[0].enabled = false;
  StopWorker(tasks.back());
  // All logical endpoints remain independently selectable.
  config->upstream_enabled = true;
  config->caster_enabled = true;
  for (auto& p : config->publishers)
    p.enabled = true;
  TickManager();
  for (const auto& slot : network_workers)
    assert(slot.live);
  NetworkAvailabilityChanged(false);
  for (const auto& task : std::vector<Task>(tasks.end() - 7, tasks.end()))
    StopWorker(task);
  for (const auto& slot : network_workers)
    assert(!slot.live);
  // A retained caster must not reuse its previous logical source incarnation
  // after worker destruction. Drive its real loop, socket and queue twice.
  config->upstream_enabled = false;
  for (auto& p : config->publishers)
    p.enabled = false;
  config->caster_port = 0;
  NetworkAvailabilityChanged(true);
  for (unsigned cycle = 0; cycle < 2; ++cycle)
  {
    config->caster_enabled = true;
    TickManager();
    const auto generation = caster_cache_generation;
    inject_caster_frame = true;
    on_delay = [&] {
      assert(caster_owner->active_source().has_value());
      config->caster_enabled = false;
    };
    StopWorker(tasks.back());
    on_delay = {};
    inject_caster_frame = false;
    assert(caster_cache_generation == generation + 1 && !caster_owner->running());
  }
  // Destruction only in this test, after all simulated workers are quiescent.
  input_owner->~NtripConnection();
  for (auto* p : publisher_owners)
    p->~NtripPublisher();
  caster_owner->~LocalRtcmCaster();
  for (auto* p : payloads)
    std::free(p);
}
#endif
