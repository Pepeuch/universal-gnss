// SPDX-License-Identifier: LGPL-3.0-only
// Runs only in the dedicated SDK/QEMU image. Never linked into shipping firmware.
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ug_credentials.hpp"
#include "ug_network_allocation.hpp"
#include "ug_pairing_pmk.h"
#include "ug_tls_bootstrap.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
#include <set>
using namespace ug::embedded;
// Read by the isolated QEMU/GDB runner. No secret or pointer crosses this
// boundary; set only after every crypto/NVS/assertion check succeeds.
extern "C" {
volatile std::uint32_t ug_tls_test_result{};
}
namespace
{
unsigned phase{};
bool armed{}, injected{}, identity_written{};
}  // namespace
extern "C" void* __real_heap_caps_calloc(std::size_t, std::size_t, std::uint32_t);
extern "C" void* __wrap_heap_caps_calloc(std::size_t count, std::size_t size, std::uint32_t caps)
{
  if (armed && phase == 4 && count * size == 6144)
  {
    injected = true;
    return nullptr;
  }
  return __real_heap_caps_calloc(count, size, caps);
}
extern "C" esp_err_t __real_nvs_set_blob(nvs_handle_t, const char*, const void*, std::size_t);
extern "C" esp_err_t
__wrap_nvs_set_blob(nvs_handle_t handle, const char* key, const void* bytes, std::size_t size)
{
  if (armed && phase == 6 && std::strcmp(key, "https") == 0)
  {
    injected = true;
    return ESP_FAIL;
  }
  const auto result = __real_nvs_set_blob(handle, key, bytes, size);
  if (armed && std::strcmp(key, "https") == 0 && result == ESP_OK)
    identity_written = true;
  return result;
}
extern "C" esp_err_t __real_nvs_commit(nvs_handle_t);
extern "C" esp_err_t __wrap_nvs_commit(nvs_handle_t handle)
{
  if (armed && phase == 8)
  {
    assert(identity_written);
    // The runner terminates the emulator here and restarts the same flash.
    // This marker is an intentional interruption, never a completed boot PASS.
    ug_tls_test_result = 0x55475400u | phase;
    vTaskSuspend(nullptr);
    return ESP_FAIL;
  }
  return __real_nvs_commit(handle);
}
// First throw at cold boot occurs in a separate 12 KiB task, not the warmed
// 32 KiB app_main caller. A task-entry catch is invisible at the Xtensa sentinel;
// the real I/O/helper handlers must remain in a called, non-inlined frame.
static std::array<std::uint8_t, 512> cold_bytes{};
static std::atomic<bool> cold_done{};
__attribute__((noinline)) static bool ColdGuardedCopy()
{
  try
  {
    NetworkAllocationScope payload(1);
    FailNextNetworkAllocationForTest(1);
    std::vector<std::uint8_t> copy(cold_bytes.begin(), cold_bytes.end());
    // Escape the address so -Os cannot elide the allocation under test.
    asm volatile("" : : "r"(copy.data()) : "memory");
    return false;
  }
  catch (const std::bad_alloc&)
  {
    return true;
  }
}
static void ColdExceptionTask(void*)
{
  for (unsigned i = 0; i < 3; ++i)
  {
    const auto before = NetworkCppAllocations()[1];
    assert(ColdGuardedCopy());
    const auto after = NetworkCppAllocations()[1];
    assert(after.refusals == before.refusals + 1 && after.live == before.live &&
           after.requested_bytes == before.requested_bytes);
    assert(!network_external_allocation && network_allocation_owner == 0);
    NetworkAllocationScope payload(1);
    auto* bytes = ::operator new(512);
    assert(esp_ptr_external_ram(bytes));
    ::operator delete(bytes);
  }
  assert(CheckNetworkArenaIntegrity() && heap_caps_check_integrity_all(true));
  cold_done.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}
static void TestColdTaskExceptionRecovery()
{
  auto* before_registry = ::operator new(4096);
  assert(esp_ptr_external_ram(before_registry));
  vTaskSuspendAll();
  ::operator delete(before_registry);
  xTaskResumeAll();
  assert(InitializeNetworkAllocation());
  assert(xTaskCreate(ColdExceptionTask, "cold_unwind", 12288, nullptr, 2, nullptr) == pdPASS);
  while (!cold_done.load(std::memory_order_acquire))
    vTaskDelay(1);
  assert(NetworkCppAllocations()[1].live == 0);
}
// Exercise the same C++ OOM recovery runtime selected by the S3 profile.
// Prewarm the unwinder, then exhaust 8-bit heaps without allocating the pointer
// ledger. The caught bad_alloc must use the configured emergency pool. No keys
// or NVS data exist yet, and every held block is returned before TLS tests.
static void TestEmergencyExceptionPool()
{
  assert(InitializeNetworkAllocation());
  auto* ordinary = ::operator new(4096);
  void* scoped{};
  {
    NetworkAllocationScope payload;
    scoped = ::operator new(37);
  }
  assert(esp_ptr_external_ram(ordinary) && esp_ptr_external_ram(scoped));
  vTaskSuspendAll();
  ::operator delete(ordinary);
  ::operator delete(scoped);
  xTaskResumeAll();
  assert(heap_caps_check_integrity_all(true));
  std::array<std::uint8_t, 16> public_key{};
  for (unsigned i = 0; i < 16; ++i)
    public_key[i] = i;
  std::array<std::uint8_t, 16> derived{};
  constexpr std::uint8_t expected[] = {0x7f,
                                       0x8d,
                                       0x3f,
                                       0x26,
                                       0xd8,
                                       0x5c,
                                       0xe3,
                                       0x09,
                                       0x5b,
                                       0x7a,
                                       0x3c,
                                       0x61,
                                       0x79,
                                       0x77,
                                       0x90,
                                       0x6b};
  assert(ug_pairing_derive_pmk(public_key.data(), derived.data()));
  assert(!std::memcmp(derived.data(), expected, 16));
  try
  {
    throw std::bad_alloc{};
  }
  catch (const std::bad_alloc&)
  {
  }
  std::array<void*, 2048> blocks{};
  std::size_t held = 0;
  std::array<void*, 65> crossing{};
  {
    NetworkAllocationScope payload;
    for (std::size_t n = 1; n <= crossing.size(); ++n)
    {
      crossing[n - 1] = ::operator new(n, std::align_val_t{64});
      assert(esp_ptr_external_ram(crossing[n - 1]) &&
             reinterpret_cast<std::uintptr_t>(crossing[n - 1]) % 64 == 0);
      std::memset(crossing[n - 1], 0x5a, n);
    }
  }
  struct Transfer
  {
    std::array<void*, 65>* pointers;
    std::atomic<bool> done{false};
  } transfer{&crossing};
  assert(xTaskCreate(
             [](void* context) {
               auto& work = *static_cast<Transfer*>(context);
               for (auto* p : *work.pointers)
                 ::operator delete(p, std::align_val_t{64});
               assert(heap_caps_check_integrity_all(true));
               work.done.store(true, std::memory_order_release);
               vTaskDelete(nullptr);
             },
             "cross_delete",
             4096,
             &transfer,
             2,
             nullptr) == pdPASS);
  while (!transfer.done.load(std::memory_order_acquire))
    vTaskDelay(1);
  assert(NetworkCppAllocations()[0].live == 0);
  std::array<std::atomic<bool>, 2> churn_done{};
  for (unsigned core = 0; core < churn_done.size(); ++core)
    assert(xTaskCreatePinnedToCore(
               [](void* context) {
                 for (unsigned i = 0; i < 256; ++i)
                 {
                   NetworkAllocationScope payload;
                   auto* p = ::operator new(37 + i % 29, std::align_val_t{64});
                   std::memset(p, 0x5a, 37 + i % 29);
                   ::operator delete(p, std::align_val_t{64});
                 }
                 static_cast<std::atomic<bool>*>(context)->store(true, std::memory_order_release);
                 vTaskDelete(nullptr);
               },
               "alloc_churn",
               4096,
               &churn_done[core],
               2,
               nullptr,
               core) == pdPASS);
  while (!churn_done[0].load(std::memory_order_acquire) ||
         !churn_done[1].load(std::memory_order_acquire))
    vTaskDelay(1);
  assert(NetworkCppAllocations()[0].live == 0 && heap_caps_check_integrity_all(true));
  // The existing timestamp monitor can legitimately retain >4096 C++ nodes.
  // Capacity is a PSRAM byte budget, not a hash bucket/block-count admission.
  const auto before_arena = NetworkArenaMemory();
  {
    NetworkAllocationScope payload;
    std::multiset<long long> history;
    for (unsigned i = 0; i < 6000; ++i)
      history.insert(i);
    assert(NetworkCppAllocations()[0].live == 6000);
  }
  assert(NetworkCppAllocations()[0].live == 0 && NetworkArenaMemory().free == before_arena.free);
  assert(CheckNetworkArenaIntegrity());
  // Exhaust only the private arena. Global PSRAM and SRAM are still available;
  // neither may silently satisfy a scoped allocation after this budget is full.
  for (const auto size : {65536u, 4096u, 256u, 32u})
  {
    NetworkAllocationScope payload;
    while (held < blocks.size())
    {
      auto* block = ::operator new(size, std::nothrow);
      if (!block)
        break;
      blocks[held++] = block;
    }
    assert(held < blocks.size());
  }
  assert(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > 65536);
  assert(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) > 1048576);
  universal_gnss_protocols::RtcmCorrectionMonitor monitor;
  universal_gnss_protocols::RtcmMessageInfo info;
  info.message_type = 1077;
  info.is_msm = true;
  info.msm_constellation = universal_gnss_protocols::RtcmConstellation::kGps;
  bool external_refused = false;
  try
  {
    NetworkAllocationScope payload;
    // Cross-component throw from the actual production timestamp container.
    monitor.ObserveMessage(info, 1);
  }
  catch (const std::bad_alloc&)
  {
    external_refused = true;
  }
  for (std::size_t i = 0; i < held; ++i)
    ::operator delete(blocks[i]);
  assert(external_refused && !network_external_allocation);
  monitor.Reset();
  assert(CheckNetworkArenaIntegrity());
  {
    NetworkAllocationScope payload;
    monitor.ObserveMessage(info, 2);
  }
  assert(monitor.valid_frames() == 1 && heap_caps_check_integrity_all(true));
  monitor.Reset();
  held = 0;
  for (const auto size : {1048576u, 65536u, 4096u, 256u, 32u})
  {
    while (held < blocks.size())
    {
      auto* block = heap_caps_malloc(size, MALLOC_CAP_8BIT);
      if (!block)
        break;
      blocks[held++] = block;
    }
    assert(held < blocks.size());
  }
  bool caught = false;
  try
  {
    auto* block = ::operator new(64);
    ::operator delete(block);
  }
  catch (const std::bad_alloc&)
  {
    caught = true;
  }
  for (std::size_t i = 0; i < held; ++i)
    heap_caps_free(blocks[i]);
  assert(caught);
}
extern "C" void app_main()
{
  TestColdTaskExceptionRecovery();
  TestEmergencyExceptionPool();
  ESP_ERROR_CHECK(nvs_flash_init());
  nvs_handle_t test{};
  ESP_ERROR_CHECK(nvs_open("ug_boot_test", NVS_READWRITE, &test));
  std::uint32_t saved_phase{};
  auto code = nvs_get_u32(test, "phase", &saved_phase);
  assert(code == ESP_OK || code == ESP_ERR_NVS_NOT_FOUND);
  phase = saved_phase;
  assert(phase <= 9);
  // Opaque public test bytes stand in for unrelated namespaces. Only this test
  // writes them; production bootstrap may access ug_identity only.
  const std::array<const char*, 7> names{
      "ug_config", "ug_auth", "ug_tls", "test_gnss", "test_ntrip", "test_wifi", "test_radio"};
  const unsigned char marker[]{1, 9, 3, 7};
  for (const auto* name : names)
  {
    nvs_handle_t handle{};
    ESP_ERROR_CHECK(nvs_open(name, NVS_READWRITE, &handle));
    if (phase == 0)
    {
      ESP_ERROR_CHECK(nvs_set_blob(handle, "sentinel", marker, sizeof(marker)));
      ESP_ERROR_CHECK(nvs_commit(handle));
    }
    nvs_close(handle);
  }
  if (phase == 2 || phase == 4 || phase == 6 || phase == 8)
  {
    nvs_handle_t handle{};
    ESP_ERROR_CHECK(nvs_open("ug_identity", NVS_READWRITE, &handle));
    if (phase == 2)
      ESP_ERROR_CHECK(nvs_set_blob(handle, "https", marker, sizeof(marker)));
    else
      ESP_ERROR_CHECK(nvs_erase_key(handle, "https"));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
  }
  if (phase == 8)
  {
    ESP_ERROR_CHECK(nvs_set_u32(test, "phase", 9));
    ESP_ERROR_CHECK(nvs_commit(test));
  }
  armed = true;
  const auto result = PrepareWebIdentityAtBoot();
  armed = false;
  if (phase == 4 || phase == 6)
  {
    assert(injected);
    assert(result.reason == (phase == 4 ? HttpsStartupReason::kNoMemory
                                        : HttpsStartupReason::kIdentityPersistenceFailed));
    LoadedWebIdentity absent;
    assert(LoadWebIdentity(absent).reason == HttpsStartupReason::kIdentityMissing);
  }
  else
  {
    assert(result.ok());
    LoadedWebIdentity identity;
    assert(LoadWebIdentity(identity).ok());
    std::array<std::uint8_t, 32> hash{}, before{};
    assert(Sha256({identity.certificate_pem(), identity.certificate_size}, hash));
    std::size_t size = before.size();
    if (phase == 1 || phase == 3)
    {
      ESP_ERROR_CHECK(nvs_get_blob(test, "cert_hash", before.data(), &size));
      assert(hash == before);
    }
    else
    {
      if (phase == 2)
      {
        ESP_ERROR_CHECK(nvs_get_blob(test, "cert_hash", before.data(), &size));
        assert(hash != before);
      }
      ESP_ERROR_CHECK(nvs_set_blob(test, "cert_hash", hash.data(), hash.size()));
    }
  }
  for (const auto* name : names)
  {
    nvs_handle_t handle{};
    ESP_ERROR_CHECK(nvs_open(name, NVS_READONLY, &handle));
    unsigned char readback[sizeof(marker)]{};
    std::size_t size = sizeof(readback);
    ESP_ERROR_CHECK(nvs_get_blob(handle, "sentinel", readback, &size));
    assert(size == sizeof(marker) && std::memcmp(readback, marker, size) == 0);
    nvs_close(handle);
  }
  ESP_ERROR_CHECK(nvs_set_u32(test, "phase", phase + 1));
  ESP_ERROR_CHECK(nvs_commit(test));
  nvs_close(test);
  std::printf(
      "UG_TLS_BOOT_TEST PASS phase=%u status=%s\n", phase, HttpsStartupReasonName(result.reason));
  std::fflush(stdout);
  ug_tls_test_result = 0x55475400u | phase;
}
