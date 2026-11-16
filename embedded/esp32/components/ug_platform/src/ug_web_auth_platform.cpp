// SPDX-License-Identifier: LGPL-3.0-only
// IDF6.1/MbedTLS4 hides its existing legacy HMAC declarations by default.
// This exposes declarations only; it changes no SDK configuration or ABI.
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "ug_web_auth_platform.hpp"
#include "bootloader_random.h"
#include "esp_random.h"
// Pinned IDF6.1/MbedTLS4 exports this tested DRBG under its new private path.
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "mbedtls/platform.h"
#include "mbedtls/private/sha256.h"
#undef MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "mbedtls/private/ctr_drbg.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "ug_admin.hpp"
#include "ug_configuration.hpp"
#include "ug_credentials.hpp"
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
    // RFC8018 PBKDF2-HMAC-SHA256, one 32-byte block, 600k iterations. Yield to
    // existing GNSS/network/idle tasks without changing their stacks/priorities.
    if (psa_crypto_init() != PSA_SUCCESS)
      return false;
    // Reuse the SDK HMAC context and its key pads for all PBKDF2 rounds.
    // Re-importing/rebuilding a PSA MAC operation each round took 137 seconds
    // on S3. This preserves exactly the same algorithm, salt and iteration count.
    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    const auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    // The default md_setup dispatches every short digest through the PSA
    // hardware driver. Use the SDK's existing software SHA256 engine for this
    // local HMAC only; TLS and global crypto configuration remain unchanged.
    // This adapter mirrors the pinned SDK md.c legacy-context ownership contract:
    // md_free owns/wipes both this allocation and the hmac_setup key pads.
    auto* sha =
        static_cast<mbedtls_sha256_context*>(mbedtls_calloc(1, sizeof(mbedtls_sha256_context)));
    context.MBEDTLS_PRIVATE(md_info) = info;
    context.MBEDTLS_PRIVATE(md_ctx) = sha;
#if defined(MBEDTLS_MD_SOME_PSA)
    context.MBEDTLS_PRIVATE(engine) = MBEDTLS_MD_ENGINE_LEGACY;
#endif
    if (sha)
      mbedtls_sha256_init(sha);
    const bool initialized =
        info && sha && mbedtls_md_hmac_setup(&context, info) == 0 &&
        mbedtls_md_hmac_starts(&context,
                               reinterpret_cast<const unsigned char*>(password.data()),
                               password.size()) == 0;
    const bool ok = initialized && PasswordKdf(
                                       record,
                                       WebAuth::kIterations,
                                       [](void* ctx,
                                          const std::uint8_t* data,
                                          std::size_t size,
                                          std::array<std::uint8_t, 32>& result) {
                                         auto* mac = static_cast<mbedtls_md_context_t*>(ctx);
                                         return mbedtls_md_hmac_reset(mac) == 0 &&
                                                mbedtls_md_hmac_update(mac, data, size) == 0 &&
                                                mbedtls_md_hmac_finish(mac, result.data()) == 0;
                                       },
                                       &context,
                                       [] { vTaskDelay(1); },
                                       out);
    mbedtls_md_free(&context);  // SDK wipes the HMAC key pads and context.
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
