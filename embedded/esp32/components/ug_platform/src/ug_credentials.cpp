// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_credentials.hpp"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"
#include <cstring>
#include <vector>
namespace ug::embedded
{
namespace
{
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
}  // namespace ug::embedded
