// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#define PSA_SUCCESS 0
#define PSA_ERROR_INSUFFICIENT_MEMORY -141
#define PSA_ERROR_INVALID_ARGUMENT -135
#define PSA_ALG_SHA_256 1
#define PSA_KEY_TYPE_IS_RSA(x) ((x) == 1)
#define PSA_KEY_TYPE_IS_ECC(x) ((x) == 2)
int psa_crypto_init();
int psa_hash_compute(
    int, const std::uint8_t*, std::size_t, std::uint8_t*, std::size_t, std::size_t*);
