// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_tls_bootstrap.hpp"
#include "bootloader_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "ug_credentials.hpp"
#include <cstdio>
#include <cstring>
namespace ug::embedded
{
namespace
{
HttpsStartupStatus boot_status;
bool prepared{};
HttpsStartupStatus CryptoStatus(int code)
{
  return {code == 0                               ? HttpsStartupReason::kReady
          : code == PSA_ERROR_INSUFFICIENT_MEMORY ? HttpsStartupReason::kNoMemory
                                                  : HttpsStartupReason::kIdentityGenerationFailed,
          code};
}
class BootIdentity final : public TlsBootstrapBackend
{
public:
  HttpsStartupStatus Load() override
  {
    LoadedWebIdentity identity;
    const auto result = LoadWebIdentity(identity);
    if (result.ok())
    {
      std::array<std::uint8_t, 32> fingerprint{};
      if (WebCertificateFingerprint(identity, fingerprint))
      {
        char text[65]{};
        constexpr char digits[] = "0123456789abcdef";
        for (std::size_t i = 0; i < fingerprint.size(); ++i)
        {
          text[2 * i] = digits[fingerprint[i] >> 4];
          text[2 * i + 1] = digits[fingerprint[i] & 15];
        }
        ESP_LOGI("UG_TLS", "certificate_sha256=%s", text);
      }
      else
        ESP_LOGW("UG_TLS", "public certificate fingerprint unavailable");
    }
    return result;
  }
  HttpsStartupStatus Generate() override
  {
    // S3 startup has PSRAM. Refuse a failed external allocation rather than
    // silently competing with UART/DMA/task stacks in internal SRAM.
    bytes = {
        static_cast<std::uint8_t*>(heap_caps_calloc(1, kSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        {kSize}};
    if (!bytes)
      return {HttpsStartupReason::kNoMemory, ESP_ERR_NO_MEM};
    std::uint8_t mac[6]{};
    const auto mac_result = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (mac_result != ESP_OK)
      return CryptoStatus(mac_result);
    char hostname[32];
    std::snprintf(hostname,
                  sizeof(hostname),
                  "ug-%02x%02x%02x%02x%02x%02x.local",
                  mac[0],
                  mac[1],
                  mac[2],
                  mac[3],
                  mac[4],
                  mac[5]);
    char subject[80];
    std::snprintf(subject,
                  sizeof(subject),
                  "CN=ug-%02x%02x%02x%02x%02x%02x.local,O=Universal GNSS",
                  mac[0],
                  mac[1],
                  mac[2],
                  mac[3],
                  mac[4],
                  mac[5]);
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    mbedtls_x509write_cert writer;
    mbedtls_x509write_crt_init(&writer);
    // Explicit startup-only entropy window; before radio, ADC or I2S owners.
    // Random serial/key/signature must not rely on a MAC-derived or fixed seed.
    int code = psa_crypto_init();
    unsigned char serial[16]{};
    if (code == 0)
      code = psa_generate_random(serial, sizeof(serial));
    serial[0] = (serial[0] & 0x7f) | 1;
    if (code == 0)
      code = psa_generate_key(&attributes, &key_id);
    if (code == 0)
      code = mbedtls_pk_copy_from_psa(key_id, &key);
    mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&writer, &key);
    mbedtls_x509write_crt_set_issuer_key(&writer, &key);
    if (code == 0)
      code = mbedtls_x509write_crt_set_serial_raw(&writer, serial, sizeof(serial));
    if (code == 0)
      code = mbedtls_x509write_crt_set_subject_name(&writer, subject);
    if (code == 0)
      code = mbedtls_x509write_crt_set_issuer_name(&writer, subject);
    // Stable MAC-specific DNS SAN. Dynamic DHCP addresses cannot be certificate
    // identities at early boot; use/pin the per-device certificate for IP access.
    unsigned char san[40]{};
    const auto hostname_size = std::strlen(hostname);
    san[0] = 0x30;
    san[1] = hostname_size + 2;
    san[2] = 0x82;
    san[3] = hostname_size;
    std::memcpy(san + 4, hostname, hostname_size);
    if (code == 0)
      code = mbedtls_x509write_crt_set_extension(&writer,
                                                 MBEDTLS_OID_SUBJECT_ALT_NAME,
                                                 MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME),
                                                 0,
                                                 san,
                                                 hostname_size + 4);
    // No clock/network prerequisite at first boot. This is a unique self-signed
    // local identity, never a publicly trusted CA. Trust remains an operator step.
    if (code == 0)
      code = mbedtls_x509write_crt_set_validity(&writer, "20200101000000", "20491231235959");
    if (code == 0)
      code = mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1);
    if (code == 0)
      code = mbedtls_x509write_crt_set_key_usage(&writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    if (code == 0)
      code = mbedtls_pk_write_key_pem(&key, bytes.get() + kCertSize, kSize - kCertSize);
    if (code == 0)
      code = mbedtls_x509write_crt_pem(&writer, bytes.get(), kCertSize);
    mbedtls_x509write_crt_free(&writer);
    mbedtls_pk_free(&key);
    if (!mbedtls_svc_key_id_is_null(key_id))
    {
      const auto destroyed = psa_destroy_key(key_id);
      if (code == 0)
        code = destroyed;
    }
    psa_reset_key_attributes(&attributes);
    return CryptoStatus(code);
  }
  HttpsStartupStatus Persist() override
  {
    return PersistWebIdentity(reinterpret_cast<const char*>(bytes.get()),
                              reinterpret_cast<const char*>(bytes.get() + kCertSize));
  }

private:
  static constexpr std::size_t kCertSize = 4096, kSize = kCertSize + 2048;
  std::unique_ptr<std::uint8_t, SecureIdentityDelete> bytes{nullptr, {}};
};
}  // namespace
HttpsStartupStatus WebIdentityBootStatus()
{
  return boot_status;
}
HttpsStartupStatus PrepareWebIdentityAtBoot()
{
  if (prepared)
    return boot_status;
  prepared = true;
  // Yield to make the main task's accumulated runtime counters bracket this
  // operation. Counters are microseconds in the pinned S3 SDK configuration.
  vTaskDelay(1);
  TaskStatus_t task_before{}, task_after{};
  vTaskGetInfo(nullptr, &task_before, pdFALSE, eInvalid);
  const auto start = esp_timer_get_time();
  const auto internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const auto external_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const auto monitoring = heap_caps_monitor_local_minimum_free_size_start();
  TlsBootstrapResult result;
  // Cover the FIRST PSA initialization too: a corrupt but parseable stored blob
  // can initialize the crypto RNG while Load() validates it, before Generate().
  bootloader_random_enable();
  {
    BootIdentity backend;
    result = BootstrapTls(backend);
  }  // Generation buffers/keys are destroyed before memory is sampled.
  bootloader_random_disable();
  boot_status = result.status;
  const auto elapsed = esp_timer_get_time() - start;
  vTaskDelay(1);
  vTaskGetInfo(nullptr, &task_after, pdFALSE, eInvalid);
  ESP_LOGI("UG_TLS",
           "bootstrap stage=%s result=%s sdk_error=%ld generated=%d recovered=%d",
           result.stage,
           HttpsStartupReasonName(result.status.reason),
           static_cast<long>(result.status.sdk_error),
           result.generated,
           result.recovered);
  ESP_LOGI(
      "UG_TLS",
      "cost elapsed_us=%lld task_cpu_us=%llu internal_before=%u internal_after=%u largest=%u "
      "internal_min=%u psram_before=%u psram_after=%u psram_min=%u stack_min=%u monitor=%d",
      static_cast<long long>(elapsed),
      static_cast<unsigned long long>(task_after.ulRunTimeCounter - task_before.ulRunTimeCounter),
      unsigned(internal_before),
      unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(external_before),
      unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)),
      unsigned(uxTaskGetStackHighWaterMark(nullptr)),
      monitoring);
  if (monitoring == ESP_OK)
  {
    const auto stopped = heap_caps_monitor_local_minimum_free_size_stop();
    if (stopped != ESP_OK)
      ESP_LOGW("UG_TLS", "memory monitor stop failed sdk_error=%d", stopped);
  }
  return boot_status;
}
}  // namespace ug::embedded
