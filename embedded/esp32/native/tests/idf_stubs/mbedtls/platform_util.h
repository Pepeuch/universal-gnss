#pragma once
#include <cstddef>
inline void mbedtls_platform_zeroize(void* p, std::size_t n)
{
  volatile unsigned char* value = static_cast<volatile unsigned char*>(p);
  while (n--)
    *value++ = 0;
}
