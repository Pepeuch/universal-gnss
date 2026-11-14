// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_credentials.hpp"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "ug_certificate_uri.hpp"
#include "ug_json.hpp"
#include <cstring>
#include <mutex>
#include <vector>
namespace ug::embedded
{
namespace
{
std::mutex credential_writer;
constexpr std::size_t kCertMax = 4096, kKeyMax = 2048;
bool Validate(const WebIdentity& id)
{
  if (id.certificate_pem.empty() || id.certificate_pem.size() > kCertMax ||
      id.private_key_pem.empty() || id.private_key_pem.size() > kKeyMax ||
      id.certificate_pem.find('\0') != std::string::npos ||
      id.private_key_pem.find('\0') != std::string::npos)
    return false;
  if (psa_crypto_init() != PSA_SUCCESS)
    return false;
  mbedtls_x509_crt cert;
  mbedtls_x509_crt_init(&cert);
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  const bool ok =
      mbedtls_x509_crt_parse(&cert,
                             reinterpret_cast<const unsigned char*>(id.certificate_pem.c_str()),
                             id.certificate_pem.size() + 1) == 0 &&
      mbedtls_pk_parse_key(&key,
                           reinterpret_cast<const unsigned char*>(id.private_key_pem.c_str()),
                           id.private_key_pem.size() + 1,
                           nullptr,
                           0) == 0 &&
      ((PSA_KEY_TYPE_IS_RSA(mbedtls_pk_get_key_type(&key)) &&
        mbedtls_pk_get_bitlen(&key) >= 2048) ||
       (PSA_KEY_TYPE_IS_ECC(mbedtls_pk_get_key_type(&key)) &&
        mbedtls_pk_get_bitlen(&key) >= 256)) &&
      mbedtls_pk_check_pair(&cert.pk, &key) == 0;
  mbedtls_pk_free(&key);
  mbedtls_x509_crt_free(&cert);
  return ok;
}
}  // namespace
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
bool StoreWebIdentity(const WebIdentity& identity, std::string& error)
{
  std::lock_guard<std::mutex> lock(credential_writer);
  if (!Validate(identity))
  {
    error = "invalid TLS certificate/private-key pair";
    return false;
  }
  std::vector<std::uint8_t> blob(4 + identity.certificate_pem.size() +
                                 identity.private_key_pem.size());
  blob[0] = identity.certificate_pem.size();
  blob[1] = identity.certificate_pem.size() >> 8;
  blob[2] = identity.private_key_pem.size();
  blob[3] = identity.private_key_pem.size() >> 8;
  std::memcpy(blob.data() + 4, identity.certificate_pem.data(), identity.certificate_pem.size());
  std::memcpy(blob.data() + 4 + identity.certificate_pem.size(),
              identity.private_key_pem.data(),
              identity.private_key_pem.size());
  nvs_handle_t handle{};
  bool ok = nvs_open("ug_identity", NVS_READWRITE, &handle) == ESP_OK;
  if (ok)
  {
    ok = nvs_set_blob(handle, "https", blob.data(), blob.size()) == ESP_OK &&
         nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
  }
  mbedtls_platform_zeroize(blob.data(), blob.size());
  if (!ok)
  {
    error = "TLS identity persistence failed";
    return false;
  }
  error.clear();
  return true;
}
bool LoadWebIdentity(WebIdentity& identity, std::string& error)
{
  nvs_handle_t handle{};
  if (nvs_open("ug_identity", NVS_READONLY, &handle) != ESP_OK)
  {
    error = "TLS identity not provisioned";
    return false;
  }
  std::size_t size = 0;
  bool ok = nvs_get_blob(handle, "https", nullptr, &size) == ESP_OK && size >= 4 &&
            size <= kCertMax + kKeyMax + 4;
  std::vector<std::uint8_t> blob(ok ? size : 0);
  if (ok)
    ok = nvs_get_blob(handle, "https", blob.data(), &size) == ESP_OK;
  nvs_close(handle);
  if (!ok)
  {
    error = "TLS identity unreadable";
    return false;
  }
  const auto cert_size = unsigned(blob[0]) | (unsigned(blob[1]) << 8),
             key_size = unsigned(blob[2]) | (unsigned(blob[3]) << 8);
  WebIdentity result;
  if (cert_size + key_size + 4 == blob.size() && cert_size <= kCertMax && key_size <= kKeyMax)
  {
    result.certificate_pem.assign(reinterpret_cast<char*>(blob.data() + 4), cert_size);
    result.private_key_pem.assign(reinterpret_cast<char*>(blob.data() + 4 + cert_size), key_size);
  }
  mbedtls_platform_zeroize(blob.data(), blob.size());
  if (!Validate(result))
  {
    error = "TLS identity invalid";
    return false;
  }
  identity = std::move(result);
  error.clear();
  return true;
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
