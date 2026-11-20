// SPDX-License-Identifier: LGPL-3.0-only
// Host provider for the production PSA C adapter; actual SDK is tested in QEMU.
#include "psa/crypto.h"
#include <cassert>
#include <cstring>
#include <openssl/crypto.h>
#include <openssl/hmac.h>
bool pmk_hash_missing{}, pmk_hash_failed{}, pmk_import_failed{}, pmk_destroy_failed{};
unsigned pmk_imports{}, pmk_destroys{};
static unsigned char held_key[16];
static bool live;
extern "C" void ug_test_pmk_failure(bool fail)
{
  pmk_hash_failed = fail;
}
extern "C" psa_status_t psa_crypto_init(void)
{
  return pmk_hash_missing ? -1 : PSA_SUCCESS;
}
extern "C" psa_status_t psa_import_key(const psa_key_attributes_t* a,
                                       const uint8_t* key,
                                       size_t size,
                                       mbedtls_svc_key_id_t* id)
{
  assert(!live && size == 16 && a->type == PSA_KEY_TYPE_HMAC && a->bits == 128 &&
         a->usage == PSA_KEY_USAGE_SIGN_MESSAGE && a->algorithm == PSA_ALG_HMAC(PSA_ALG_SHA_256));
  if (pmk_import_failed)
    return -1;
  std::memcpy(held_key, key, 16);
  live = true;
  *id = 1;
  ++pmk_imports;
  return PSA_SUCCESS;
}
extern "C" psa_status_t psa_mac_compute(mbedtls_svc_key_id_t id,
                                        psa_algorithm_t alg,
                                        const uint8_t* input,
                                        size_t size,
                                        uint8_t* out,
                                        size_t capacity,
                                        size_t* written)
{
  assert(live && id == 1 && alg == PSA_ALG_HMAC(PSA_ALG_SHA_256) && capacity == 32);
  if (pmk_hash_failed)
    return -1;
  unsigned length{};
  if (!HMAC(EVP_sha256(), held_key, 16, input, size, out, &length))
    return -1;
  *written = length;
  return PSA_SUCCESS;
}
extern "C" psa_status_t psa_destroy_key(mbedtls_svc_key_id_t id)
{
  assert(live && id == 1);
  OPENSSL_cleanse(held_key, 16);
  live = false;
  ++pmk_destroys;
  return pmk_destroy_failed ? -1 : PSA_SUCCESS;
}
