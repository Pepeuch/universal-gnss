#pragma once
#include <stddef.h>
static inline void mbedtls_platform_zeroize(void* p, size_t n)
{
  volatile unsigned char* value = (volatile unsigned char*)p;
  while (n--)
    *value++ = 0;
}
