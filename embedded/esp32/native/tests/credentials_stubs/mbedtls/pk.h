// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "psa/crypto.h"
struct mbedtls_pk_context
{
  int type{};
  std::size_t bits{};
};
void mbedtls_pk_init(mbedtls_pk_context*);
void mbedtls_pk_free(mbedtls_pk_context*);
int mbedtls_pk_parse_key(
    mbedtls_pk_context*, const unsigned char*, std::size_t, const unsigned char*, std::size_t);
int mbedtls_pk_check_pair(const mbedtls_pk_context*, const mbedtls_pk_context*);
inline int mbedtls_pk_get_key_type(const mbedtls_pk_context* key)
{
  return key->type;
}
inline std::size_t mbedtls_pk_get_bitlen(const mbedtls_pk_context* key)
{
  return key->bits;
}
