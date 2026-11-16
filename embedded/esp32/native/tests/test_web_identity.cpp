// SPDX-License-Identifier: LGPL-3.0-only
// Fault injection at NVS/PSA boundaries; this is not a crypto implementation.
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "ug_credentials.hpp"
#include "ug_https_server_status.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace ug::embedded;
namespace
{
int open_error{}, info_error{}, read_error{}, crypto_error{}, cert_error{}, pair_error{};
unsigned closes{}, writes{};
bool fail_malloc{}, weak_key{};
std::size_t reported_size{};
void* watched{};
std::size_t watched_size{};
unsigned wiped{};
std::vector<unsigned char> blob;
void Good()
{
  open_error = info_error = read_error = crypto_error = cert_error = pair_error = 0;
  closes = writes = 0;
  weak_key = fail_malloc = false;
  blob = {4, 0, 3, 0, 'C', 'E', 'R', 'T', 'K', 'E', 'Y'};
  reported_size = blob.size();
}
void Expect(HttpsStartupReason reason)
{
  LoadedWebIdentity identity;
  const auto result = LoadWebIdentity(identity);
  assert(result.reason == reason);
  assert(!identity.bytes);
  assert(writes == 0);
}
}  // namespace
extern "C" void* __real_malloc(std::size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(std::size_t size)
{
  if (fail_malloc)
  {
    fail_malloc = false;
    return nullptr;
  }
  return __real_malloc(size);
}
extern "C" void __wrap_free(void* pointer)
{
  if (pointer && pointer == watched)
  {
    const auto* bytes = static_cast<const unsigned char*>(pointer);
    for (std::size_t i = 0; i < watched_size; ++i)
      assert(bytes[i] == 0);
    ++wiped;
    watched = nullptr;
  }
  __real_free(pointer);
}
esp_err_t nvs_open(const char* name, int mode, nvs_handle_t* handle)
{
  assert(std::strcmp(name, "ug_identity") == 0);
  assert(mode == NVS_READONLY);
  *handle = 1;
  return open_error;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* name, void* out, std::size_t* size)
{
  assert(handle == 1 && std::strcmp(name, "https") == 0);
  if (!out)
  {
    *size = reported_size;
    return info_error;
  }
  if (read_error)
    return read_error;
  assert(*size >= blob.size());
  std::memcpy(out, blob.data(), blob.size());
  *size = blob.size();
  return ESP_OK;
}
void nvs_close(nvs_handle_t)
{
  ++closes;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, std::size_t)
{
  ++writes;
  return ESP_FAIL;
}
esp_err_t nvs_commit(nvs_handle_t)
{
  ++writes;
  return ESP_FAIL;
}
esp_err_t nvs_erase_key(nvs_handle_t, const char*)
{
  ++writes;
  return ESP_FAIL;
}
int psa_crypto_init()
{
  return crypto_error;
}
int psa_hash_compute(
    int, const std::uint8_t*, std::size_t, std::uint8_t*, std::size_t, std::size_t*)
{
  return PSA_ERROR_INVALID_ARGUMENT;
}
void mbedtls_pk_init(mbedtls_pk_context* key)
{
  *key = {};
}
void mbedtls_pk_free(mbedtls_pk_context*)
{
}
void mbedtls_x509_crt_init(mbedtls_x509_crt* cert)
{
  *cert = {};
}
void mbedtls_x509_crt_free(mbedtls_x509_crt*)
{
}
int mbedtls_x509_crt_parse(mbedtls_x509_crt* cert, const unsigned char* input, std::size_t size)
{
  if (cert_error)
    return cert_error;
  assert(size == 5 && std::memcmp(input, "CERT", 5) == 0);
  cert->pk = {2, 256};
  return 0;
}
int mbedtls_pk_parse_key(mbedtls_pk_context* key,
                         const unsigned char* input,
                         std::size_t size,
                         const unsigned char*,
                         std::size_t)
{
  assert(size == 4 && std::memcmp(input, "KEY", 4) == 0);
  *key = {2, weak_key ? 128u : 256u};
  return 0;
}
int mbedtls_pk_check_pair(const mbedtls_pk_context*, const mbedtls_pk_context*)
{
  return pair_error;
}
int main()
{
  assert(LastHttpsStartupStatus().reason == HttpsStartupReason::kNotStarted);
  Good();
  open_error = ESP_ERR_NVS_NOT_FOUND;
  Expect(HttpsStartupReason::kIdentityMissing);
  assert(!closes);
  Good();
  info_error = ESP_ERR_NVS_NOT_FOUND;
  Expect(HttpsStartupReason::kIdentityMissing);
  assert(closes == 1);
  Good();
  open_error = ESP_FAIL;
  Expect(HttpsStartupReason::kIdentityUnreadable);
  Good();
  read_error = ESP_FAIL;
  Expect(HttpsStartupReason::kIdentityUnreadable);
  assert(closes == 1);
  Good();
  open_error = ESP_ERR_NO_MEM;
  Expect(HttpsStartupReason::kNoMemory);
  Good();
  fail_malloc = true;
  Expect(HttpsStartupReason::kNoMemory);
  assert(closes == 1);
  Good();
  crypto_error = PSA_ERROR_INSUFFICIENT_MEMORY;
  Expect(HttpsStartupReason::kNoMemory);
  Good();
  crypto_error = PSA_ERROR_INVALID_ARGUMENT;
  Expect(HttpsStartupReason::kCryptoUnavailable);
  Good();
  cert_error = PSA_ERROR_INSUFFICIENT_MEMORY;
  Expect(HttpsStartupReason::kNoMemory);
  Good();
  cert_error = PSA_ERROR_INVALID_ARGUMENT;
  Expect(HttpsStartupReason::kIdentityInvalid);
  Good();
  pair_error = PSA_ERROR_INVALID_ARGUMENT;
  Expect(HttpsStartupReason::kIdentityInvalid);
  Good();
  weak_key = true;
  Expect(HttpsStartupReason::kIdentityInvalid);
  for (const auto size : {std::size_t(0), std::size_t(3), std::size_t(6149)})
  {
    Good();
    reported_size = size;
    Expect(HttpsStartupReason::kIdentityInvalid);
  }
  Good();
  blob[0] = 0;
  Expect(HttpsStartupReason::kIdentityInvalid);
  Good();
  blob[5] = 0;
  Expect(HttpsStartupReason::kIdentityInvalid);
  Good();
  reported_size += 1;
  Expect(HttpsStartupReason::kIdentityUnreadable);
  Good();
  LoadedWebIdentity identity;
  assert(LoadWebIdentity(identity).ok());
  assert(identity.certificate_size == 4 && identity.private_key_size == 3);
  assert(std::strcmp(identity.certificate_pem(), "CERT") == 0);
  assert(std::strcmp(identity.private_key_pem(), "KEY") == 0);
  assert(blob == std::vector<unsigned char>({4, 0, 3, 0, 'C', 'E', 'R', 'T', 'K', 'E', 'Y'}));
  watched = identity.bytes.get();
  watched_size = blob.size() + 2;
  open_error = ESP_ERR_NVS_NOT_FOUND;
  assert(LoadWebIdentity(identity).reason == HttpsStartupReason::kIdentityMissing);
  assert(wiped == 1 && !identity.bytes && !writes);
  assert(HttpsServerFailureReason(ESP_ERR_NO_MEM, false) == HttpsStartupReason::kNoMemory);
  assert(HttpsServerFailureReason(ESP_ERR_HTTPD_ALLOC_MEM, false) == HttpsStartupReason::kNoMemory);
  assert(HttpsServerFailureReason(ESP_ERR_INVALID_STATE, false) ==
         HttpsStartupReason::kServerStartFailed);
  assert(HttpsServerFailureReason(ESP_FAIL, true) == HttpsStartupReason::kRouteRegistrationFailed);
  assert(HttpsServerFailureReason(ESP_ERR_HTTPD_ALLOC_MEM, true) == HttpsStartupReason::kNoMemory);
  for (const auto reason : {HttpsStartupReason::kIdentityMissing,
                            HttpsStartupReason::kIdentityUnreadable,
                            HttpsStartupReason::kIdentityInvalid,
                            HttpsStartupReason::kNoMemory,
                            HttpsStartupReason::kServerStartFailed,
                            HttpsStartupReason::kReady})
  {
    assert(RecordHttpsStartupStatus(reason, 17) == static_cast<std::int32_t>(reason));
    const auto sample = LastHttpsStartupStatus();
    assert(sample.reason == reason && sample.sdk_error == 17);
    assert(std::strcmp(HttpsStartupReasonName(reason), "unspecified") != 0);
  }
}
