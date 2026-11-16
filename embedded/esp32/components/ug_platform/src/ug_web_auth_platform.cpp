// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_web_auth_platform.hpp"
#include "bootloader_random.h"
#include "esp_random.h"
// Pinned IDF6.1/MbedTLS4 exports this tested DRBG under its new private path.
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/private/ctr_drbg.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "psa_crypto_driver_esp_sha.h"
extern "C" {
#include "psa_crypto_driver_esp_sha256.h"
}
#include "sha/sha_core.h"
#include "ug_admin.hpp"
#include "ug_configuration.hpp"
#include "ug_credentials.hpp"
#include "ug_password_hmac.hpp"
#include "ug_password_kdf.hpp"
#include <cstring>
namespace ug::embedded
{
namespace
{
// Entropy is collected only during S3 startup, never while radio/ADC are active.
mbedtls_ctr_drbg_context session_random;
bool random_ready{};
bool collecting_entropy{};
int SeedEntropy(void*, unsigned char* bytes, std::size_t size)
{
  if (!collecting_entropy)
    return MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;
  esp_fill_random(bytes, size);
  return 0;
}
using Record = std::array<std::uint8_t, 56>;
// Pinned IDF 6.1 SHA driver, using caller-owned contexts. Unlike the general
// PSA hash clone dispatcher, these operations do not allocate per HMAC round.
// The SDK owns padding, SHA state cloning and hardware arbitration; each hash
// block releases the SHA/AES lock before PasswordKdf can yield to other tasks.
class PasswordSha256
{
public:
  ~PasswordSha256()
  {
    Reset();
  }
  bool Start()
  {
    return esp_sha256_starts(&context_, SHA2_256) == PSA_SUCCESS;
  }
  bool Update(const std::uint8_t* data, std::size_t size)
  {
    return !size || esp_sha256_driver_update(&context_, data, size) == PSA_SUCCESS;
  }
  bool Clone(const PasswordSha256& source)
  {
    return esp_sha256_driver_clone(&source.context_, &context_) == PSA_SUCCESS;
  }
  bool Finish(std::array<std::uint8_t, 32>& out)
  {
    std::size_t size{};
    return esp_sha256_driver_finish(
               &context_, out.data(), out.size(), &size, ESP_SHA_OPERATION_TYPE_SHA256) ==
               PSA_SUCCESS &&
           size == out.size();
  }
  void Reset()
  {
    esp_sha256_driver_abort(&context_);
  }

private:
  esp_sha256_context context_{};
};
class Backend final : public AuthBackend
{
public:
  AuthRead Read(PasswordRecord& record) override
  {
    nvs_handle_t h{};
    const auto opened = nvs_open("ug_auth", NVS_READONLY, &h);
    if (opened == ESP_ERR_NVS_NOT_FOUND)
      return AuthRead::kMissing;
    if (opened != ESP_OK)
      return AuthRead::kError;
    Record bytes{};
    std::size_t n = bytes.size();
    const auto err = nvs_get_blob(h, "password", bytes.data(), &n);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND)
      return AuthRead::kMissing;
    const std::uint8_t magic[] = {'U', 'G', 'A', 'U', 1, 0, 0, 0};
    const bool valid = err == ESP_OK && n == bytes.size() && !std::memcmp(bytes.data(), magic, 8);
    if (valid)
    {
      std::memcpy(record.salt.data(), bytes.data() + 8, 16);
      std::memcpy(record.hash.data(), bytes.data() + 24, 32);
    }
    Wipe(bytes.data(), bytes.size());
    return valid ? AuthRead::kFound : AuthRead::kError;
  }
  bool Save(const PasswordRecord& record) override
  {
    Record bytes{'U', 'G', 'A', 'U', 1, 0, 0, 0}, check{};
    std::memcpy(bytes.data() + 8, record.salt.data(), 16);
    std::memcpy(bytes.data() + 24, record.hash.data(), 32);
    nvs_handle_t h{};
    std::size_t n = check.size();
    bool ok = nvs_open("ug_auth", NVS_READWRITE, &h) == ESP_OK;
    if (ok)
    {
      ok = nvs_set_blob(h, "password", bytes.data(), bytes.size()) == ESP_OK &&
           nvs_commit(h) == ESP_OK && nvs_get_blob(h, "password", check.data(), &n) == ESP_OK &&
           n == bytes.size() && bytes == check;
      nvs_close(h);
    }
    Wipe(bytes.data(), bytes.size());
    Wipe(check.data(), check.size());
    return ok;
  }
  bool Random(std::uint8_t* out, std::size_t size) override
  {
    return random_ready && mbedtls_ctr_drbg_random(&session_random, out, size) == 0;
  }
  bool Legacy(std::string_view value, std::string_view expected) override
  {
    return expected == Configuration()->admin_token_sha256 &&
           AuthorizedBearer(std::string("Bearer ") + std::string(value), expected, Sha256);
  }
  bool Derive(std::string_view password,
              const PasswordRecord& record,
              std::array<std::uint8_t, 32>& out) override
  {
    // RFC8018 parameters and the v1 NVS record stay unchanged. Cache only
    // keyed prefix states for this derivation, never a reusable password proof.
    out = {};
    if (psa_crypto_init() != PSA_SUCCESS)
      return false;
    PasswordHmac<PasswordSha256> hmac;
    const bool ok =
        hmac.Initialize(password) &&
        PasswordKdf(
            record,
            WebAuth::kIterations,
            [](void* ctx,
               const std::uint8_t* data,
               std::size_t size,
               std::array<std::uint8_t, 32>& result) {
              return static_cast<PasswordHmac<PasswordSha256>*>(ctx)->Compute(data, size, result);
            },
            &hmac,
            [] { vTaskDelay(1); },
            out);
    if (!ok)
      Wipe(out.data(), out.size());
    return ok;
  }
};
Backend backend;
WebAuth auth(backend);
}  // namespace
bool InitializeWebSessionRandom()
{
  static bool attempted{};
  if (attempted)
    return random_ready;
  attempted = true;
  // Caller owns early startup; no ADC, Wi-Fi or Bluetooth can race this source.
  bootloader_random_enable();
  mbedtls_ctr_drbg_init(&session_random);
  constexpr unsigned char personalization[] = "UG Web sessions v1";
  collecting_entropy = true;
  const int result = mbedtls_ctr_drbg_seed(
      &session_random, SeedEntropy, nullptr, personalization, sizeof(personalization) - 1);
  collecting_entropy = false;
  bootloader_random_disable();
  // The retained callback refuses reseeding after startup rather than silently
  // using an RNG whose runtime entropy requirements are no longer guaranteed.
  if (result != 0)
  {
    mbedtls_ctr_drbg_free(&session_random);
    return false;
  }
  random_ready = true;
  return true;
}
WebAuth& WebAuthentication()
{
  static bool initialized = false;
  if (!initialized)
  {
    const auto config = Configuration();
    const bool fresh = ConfigurationWritable() && ConfigurationBanks().generation == 0 &&
                       config->admin_token_sha256.empty();
    auth.Start(fresh, config->admin_token_sha256);
    initialized = true;
  }
  return auth;
}
bool BenchmarkWebPassword(std::int64_t& elapsed_us)
{
  PasswordRecord record{};
  for (unsigned i = 0; i < record.salt.size(); ++i)
    record.salt[i] = i;
  constexpr std::array<std::uint8_t, 32> expected{
      59,  195, 113, 24, 230, 37,  9,   62, 155, 121, 237, 8,  147, 14, 167, 175,
      115, 137, 89,  18, 51,  253, 217, 45, 221, 243, 105, 55, 30,  96, 219, 192};
  std::array<std::uint8_t, 32> result{};
  const auto begin = esp_timer_get_time();
  const bool ok = backend.Derive("password", record, result) && result == expected;
  elapsed_us = esp_timer_get_time() - begin;
  Wipe(result.data(), result.size());
  return ok;
}
}  // namespace ug::embedded
