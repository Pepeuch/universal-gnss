// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_credentials.hpp"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "ug_certificate_uri.hpp"
#include "ug_json.hpp"
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
namespace ug::embedded
{
namespace
{
std::mutex credential_writer;
constexpr std::size_t kCertMax = 4096, kKeyMax = 2048;
HttpsStartupStatus
Validate(const char* cert_pem, std::size_t cert_size, const char* key_pem, std::size_t key_size)
{
  if (!cert_pem || !key_pem || !cert_size || cert_size > kCertMax || !key_size ||
      key_size > kKeyMax || std::memchr(cert_pem, 0, cert_size) ||
      std::memchr(key_pem, 0, key_size))
    return {HttpsStartupReason::kIdentityInvalid, ESP_ERR_INVALID_ARG};
  const auto crypto = psa_crypto_init();
  if (crypto != PSA_SUCCESS)
    return {crypto == PSA_ERROR_INSUFFICIENT_MEMORY ? HttpsStartupReason::kNoMemory
                                                    : HttpsStartupReason::kCryptoUnavailable,
            crypto};
  mbedtls_x509_crt cert;
  mbedtls_x509_crt_init(&cert);
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  auto code = mbedtls_x509_crt_parse(
      &cert, reinterpret_cast<const unsigned char*>(cert_pem), cert_size + 1);
  if (code == 0)
    code = mbedtls_pk_parse_key(
        &key, reinterpret_cast<const unsigned char*>(key_pem), key_size + 1, nullptr, 0);
  if (code == 0 &&
      !((PSA_KEY_TYPE_IS_RSA(mbedtls_pk_get_key_type(&key)) &&
         mbedtls_pk_get_bitlen(&key) >= 2048) ||
        (PSA_KEY_TYPE_IS_ECC(mbedtls_pk_get_key_type(&key)) && mbedtls_pk_get_bitlen(&key) >= 256)))
    code = PSA_ERROR_INVALID_ARGUMENT;
  if (code == 0)
    code = mbedtls_pk_check_pair(&cert.pk, &key);
  mbedtls_pk_free(&key);
  mbedtls_x509_crt_free(&cert);
  return {code == 0                               ? HttpsStartupReason::kReady
          : code == PSA_ERROR_INSUFFICIENT_MEMORY ? HttpsStartupReason::kNoMemory
                                                  : HttpsStartupReason::kIdentityInvalid,
          code};
}
}  // namespace
void SecureIdentityDelete::operator()(std::uint8_t* bytes) const
{
  if (bytes)
    mbedtls_platform_zeroize(bytes, size);
  std::free(bytes);
}
bool Sha256(std::string_view bytes, std::array<std::uint8_t, 32>& out)
{
  std::size_t length = 0;
  return psa_crypto_init() == PSA_SUCCESS &&
         psa_hash_compute(PSA_ALG_SHA_256,
                          reinterpret_cast<const std::uint8_t*>(bytes.data()),
                          bytes.size(),
                          out.data(),
                          out.size(),
                          &length) == PSA_SUCCESS &&
         length == out.size();
}
HttpsStartupStatus PersistWebIdentity(std::string_view certificate, std::string_view key)
{
  std::lock_guard<std::mutex> lock(credential_writer);
  // Callers provide NUL-terminated PEM spans (NUL excluded from the views).
  const auto validated = Validate(certificate.data(), certificate.size(), key.data(), key.size());
  if (!validated.ok())
    return validated;
  const auto size = 4 + certificate.size() + key.size();
  std::unique_ptr<std::uint8_t, SecureIdentityDelete> blob{
      static_cast<std::uint8_t*>(std::malloc(size)), {size}};
  if (!blob)
    return {HttpsStartupReason::kNoMemory, ESP_ERR_NO_MEM};
  blob.get()[0] = certificate.size();
  blob.get()[1] = certificate.size() >> 8;
  blob.get()[2] = key.size();
  blob.get()[3] = key.size() >> 8;
  std::memcpy(blob.get() + 4, certificate.data(), certificate.size());
  std::memcpy(blob.get() + 4 + certificate.size(), key.data(), key.size());
  nvs_handle_t handle{};
  auto code = nvs_open("ug_identity", NVS_READWRITE, &handle);
  if (code == ESP_OK)
  {
    // One atomic blob: interruption cannot expose a new certificate with an old key.
    code = nvs_set_blob(handle, "https", blob.get(), size);
    if (code == ESP_OK)
      code = nvs_commit(handle);
    nvs_close(handle);
  }
  return {code == ESP_OK           ? HttpsStartupReason::kReady
          : code == ESP_ERR_NO_MEM ? HttpsStartupReason::kNoMemory
                                   : HttpsStartupReason::kIdentityPersistenceFailed,
          code};
}
bool StoreWebIdentity(const WebIdentity& identity, std::string& error)
{
  const auto result = PersistWebIdentity(identity.certificate_pem, identity.private_key_pem);
  error = result.ok() ? "" : HttpsStartupReasonName(result.reason);
  return result.ok();
}
HttpsStartupStatus LoadWebIdentity(LoadedWebIdentity& identity)
{
  std::lock_guard<std::mutex> lock(credential_writer);
  identity = {};
  nvs_handle_t handle{};
  auto code = nvs_open("ug_identity", NVS_READONLY, &handle);
  const auto read_error = [](esp_err_t value) -> HttpsStartupStatus {
    return {value == ESP_ERR_NVS_NOT_FOUND ? HttpsStartupReason::kIdentityMissing
            : value == ESP_ERR_NO_MEM      ? HttpsStartupReason::kNoMemory
                                           : HttpsStartupReason::kIdentityUnreadable,
            value};
  };
  if (code != ESP_OK)
    return read_error(code);
  std::size_t size = 0;
  code = nvs_get_blob(handle, "https", nullptr, &size);
  if (code != ESP_OK)
  {
    nvs_close(handle);
    return read_error(code);
  }
  if (size < 4 || size > kCertMax + kKeyMax + 4)
  {
    nvs_close(handle);
    return {HttpsStartupReason::kIdentityInvalid, ESP_ERR_INVALID_SIZE};
  }
  LoadedWebIdentity next;
  next.bytes = {static_cast<std::uint8_t*>(std::malloc(size + 2)), {size + 2}};
  if (!next.bytes)
  {
    nvs_close(handle);
    return {HttpsStartupReason::kNoMemory, ESP_ERR_NO_MEM};
  }
  auto actual = size;
  code = nvs_get_blob(handle, "https", next.bytes.get(), &actual);
  nvs_close(handle);
  if (code != ESP_OK)
    return read_error(code);
  if (actual != size)
    return {HttpsStartupReason::kIdentityUnreadable, ESP_ERR_INVALID_SIZE};
  auto* blob = next.bytes.get();
  const auto cert_size = unsigned(blob[0]) | (unsigned(blob[1]) << 8);
  const auto key_size = unsigned(blob[2]) | (unsigned(blob[3]) << 8);
  if (!cert_size || !key_size || cert_size > kCertMax || key_size > kKeyMax ||
      cert_size + key_size + 4 != size)
    return {HttpsStartupReason::kIdentityInvalid, ESP_ERR_INVALID_SIZE};
  // NVS format unchanged. Insert only RAM terminators for the PEM parsers.
  std::memmove(blob + 5 + cert_size, blob + 4 + cert_size, key_size);
  blob[4 + cert_size] = 0;
  blob[size + 1] = 0;
  next.certificate_size = cert_size;
  next.private_key_size = key_size;
  const auto valid = Validate(next.certificate_pem(), cert_size, next.private_key_pem(), key_size);
  if (valid.ok())
    identity = std::move(next);
  return valid;
}
bool WebCertificateFingerprint(const LoadedWebIdentity& identity,
                               std::array<std::uint8_t, 32>& fingerprint)
{
  if (!identity.bytes)
    return false;
  mbedtls_x509_crt cert;
  mbedtls_x509_crt_init(&cert);
  const auto parsed =
      mbedtls_x509_crt_parse(&cert,
                             reinterpret_cast<const unsigned char*>(identity.certificate_pem()),
                             identity.certificate_size + 1);
  const bool ok = parsed == 0 && cert.raw.p && cert.raw.len &&
                  Sha256({reinterpret_cast<const char*>(cert.raw.p), cert.raw.len}, fingerprint);
  mbedtls_x509_crt_free(&cert);
  return ok;
}
bool StoreTlsObject(std::string_view name,
                    std::string_view pem,
                    bool private_key,
                    std::string& error)
{
  std::lock_guard<std::mutex> lock(credential_writer);
  if (!ValidCertificateName(name) || pem.empty() || pem.size() > (private_key ? 2048 : 4096) ||
      pem.find('\0') != std::string_view::npos)
  {
    error = "invalid TLS object name/size";
    return false;
  }
  if (psa_crypto_init() != PSA_SUCCESS)
  {
    error = "crypto subsystem unavailable";
    return false;
  }
  std::string input(pem);
  bool valid = false;
  if (private_key)
  {
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    valid = mbedtls_pk_parse_key(&key,
                                 reinterpret_cast<const unsigned char*>(input.c_str()),
                                 input.size() + 1,
                                 nullptr,
                                 0) == 0 &&
            ((PSA_KEY_TYPE_IS_RSA(mbedtls_pk_get_key_type(&key)) &&
              mbedtls_pk_get_bitlen(&key) >= 2048) ||
             (PSA_KEY_TYPE_IS_ECC(mbedtls_pk_get_key_type(&key)) &&
              mbedtls_pk_get_bitlen(&key) >= 256));
    mbedtls_pk_free(&key);
  }
  else
  {
    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    valid = mbedtls_x509_crt_parse(&cert,
                                   reinterpret_cast<const unsigned char*>(input.c_str()),
                                   input.size() + 1) == 0;
    mbedtls_x509_crt_free(&cert);
  }
  mbedtls_platform_zeroize(input.data(), input.size());
  if (!valid)
  {
    error = "invalid TLS certificate/key";
    return false;
  }
  nvs_handle_t handle{};
  bool ok = nvs_open("ug_tls", NVS_READWRITE, &handle) == ESP_OK;
  if (ok)
  {
    const std::string key(name);
    ok = nvs_set_blob(handle, key.c_str(), pem.data(), pem.size()) == ESP_OK &&
         nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
  }
  if (!ok)
  {
    error = "TLS object could not be committed";
    return false;
  }
  error.clear();
  return true;
}
bool DeleteTlsObject(std::string_view name, std::string& error)
{
  std::lock_guard<std::mutex> lock(credential_writer);
  if (!ValidCertificateName(name))
  {
    error = "invalid TLS object name";
    return false;
  }
  nvs_handle_t handle{};
  bool ok = nvs_open("ug_tls", NVS_READWRITE, &handle) == ESP_OK;
  if (ok)
  {
    const std::string key(name);
    ok = nvs_erase_key(handle, key.c_str()) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
  }
  if (!ok)
  {
    error = "TLS object deletion failed";
    return false;
  }
  error.clear();
  return true;
}
bool ApplyTlsObject(std::string_view body, std::string& error)
{
  auto root = ParseBoundedObject(body, error);
  if (!root)
    return false;
  error = "invalid TLS object operation or missing confirmation";
  if (!OnlyKeys(root.get(), {"name", "kind", "pem", "confirm"}))
    return false;
  auto* name = cJSON_GetObjectItemCaseSensitive(root.get(), "name");
  auto* kind = cJSON_GetObjectItemCaseSensitive(root.get(), "kind");
  auto* pem = cJSON_GetObjectItemCaseSensitive(root.get(), "pem");
  auto* confirm = cJSON_GetObjectItemCaseSensitive(root.get(), "confirm");
  if (!cJSON_IsString(name) || !cJSON_IsString(kind) || !cJSON_IsString(confirm))
    return false;
  if (std::strcmp(kind->valuestring, "delete") == 0)
  {
    if (pem || std::strcmp(confirm->valuestring, "DELETE_TLS_OBJECT") != 0)
      return false;
    return DeleteTlsObject(name->valuestring, error);
  }
  if (!cJSON_IsString(pem) || std::strcmp(confirm->valuestring, "REPLACE_TLS_OBJECT") != 0)
    return false;
  if (std::strcmp(kind->valuestring, "certificate") != 0 &&
      std::strcmp(kind->valuestring, "private-key") != 0)
    return false;
  return StoreTlsObject(name->valuestring,
                        pem->valuestring,
                        std::strcmp(kind->valuestring, "private-key") == 0,
                        error);
}
}  // namespace ug::embedded
