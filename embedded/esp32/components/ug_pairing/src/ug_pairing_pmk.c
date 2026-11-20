// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_pairing_pmk.h"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"
#include <stddef.h>
#include <string.h>
bool ug_pairing_derive_pmk(const uint8_t lmk[16], uint8_t pmk[16])
{
  static const unsigned char domain[] = "Universal GNSS ESP-NOW PMK v1";
  if (!lmk || !pmk)
    return false;
  unsigned char digest[32] = {0};
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
  psa_set_key_bits(&attributes, 128);
  psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
  psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
  mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
  bool ok = psa_crypto_init() == PSA_SUCCESS;
  if (ok)
    ok = psa_import_key(&attributes, lmk, 16, &key) == PSA_SUCCESS;
  psa_reset_key_attributes(&attributes);
  if (ok)
  {
    size_t length = 0;
    ok = psa_mac_compute(key,
                         PSA_ALG_HMAC(PSA_ALG_SHA_256),
                         domain,
                         sizeof(domain) - 1,
                         digest,
                         sizeof(digest),
                         &length) == PSA_SUCCESS &&
         length == sizeof(digest);
    // Attributes use the default volatile lifetime. Never persist another key.
    const bool destroyed = psa_destroy_key(key) == PSA_SUCCESS;
    ok = ok && destroyed;
  }
  if (ok)
    memcpy(pmk, digest, 16);
  else
    mbedtls_platform_zeroize(pmk, 16);
  mbedtls_platform_zeroize(digest, sizeof(digest));
  return ok;
}
