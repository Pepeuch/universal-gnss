// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int32_t psa_status_t;
typedef uint32_t mbedtls_svc_key_id_t;
typedef uint32_t psa_algorithm_t;
typedef struct
{
  unsigned type, bits, usage, algorithm;
} psa_key_attributes_t;
#define PSA_KEY_ATTRIBUTES_INIT {0, 0, 0, 0}
#define MBEDTLS_SVC_KEY_ID_INIT 0
#define PSA_SUCCESS 0
#define PSA_KEY_TYPE_HMAC 0x1100u
#define PSA_KEY_USAGE_SIGN_MESSAGE 0x0400u
#define PSA_ALG_SHA_256 0x02000009u
#define PSA_ALG_HMAC(hash) (0x03800000u | ((hash) & 0xffu))
static inline void psa_set_key_type(psa_key_attributes_t* a, unsigned v)
{
  a->type = v;
}
static inline void psa_set_key_bits(psa_key_attributes_t* a, unsigned v)
{
  a->bits = v;
}
static inline void psa_set_key_usage_flags(psa_key_attributes_t* a, unsigned v)
{
  a->usage = v;
}
static inline void psa_set_key_algorithm(psa_key_attributes_t* a, unsigned v)
{
  a->algorithm = v;
}
static inline void psa_reset_key_attributes(psa_key_attributes_t* a)
{
  *a = (psa_key_attributes_t)PSA_KEY_ATTRIBUTES_INIT;
}
psa_status_t psa_crypto_init(void);
psa_status_t
psa_import_key(const psa_key_attributes_t*, const uint8_t*, size_t, mbedtls_svc_key_id_t*);
psa_status_t psa_mac_compute(
    mbedtls_svc_key_id_t, psa_algorithm_t, const uint8_t*, size_t, uint8_t*, size_t, size_t*);
psa_status_t psa_destroy_key(mbedtls_svc_key_id_t);
#ifdef __cplusplus
}
#endif
