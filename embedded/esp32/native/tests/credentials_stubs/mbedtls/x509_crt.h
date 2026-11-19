// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "pk.h"
struct mbedtls_x509_crt
{
  mbedtls_pk_context pk;
  struct
  {
    const unsigned char* p{};
    std::size_t len{};
  } raw;
};
void mbedtls_x509_crt_init(mbedtls_x509_crt*);
void mbedtls_x509_crt_free(mbedtls_x509_crt*);
int mbedtls_x509_crt_parse(mbedtls_x509_crt*, const unsigned char*, std::size_t);
