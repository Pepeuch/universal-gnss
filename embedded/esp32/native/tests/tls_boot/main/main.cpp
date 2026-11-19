// SPDX-License-Identifier: LGPL-3.0-only
// Runs only in the dedicated SDK/QEMU image. Never linked into shipping firmware.
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ug_credentials.hpp"
#include "ug_tls_bootstrap.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
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
extern "C" void app_main()
{
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
