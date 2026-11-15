// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
inline void mbedtls_platform_zeroize(void* bytes, std::size_t size)
{
  auto* p = static_cast<volatile unsigned char*>(bytes);
  while (size--)
    *p++ = 0;
}
