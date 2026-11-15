// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#define MALLOC_CAP_SPIRAM (1U << 10)
#define MALLOC_CAP_8BIT (1U << 2)
void* heap_caps_malloc(std::size_t, std::uint32_t);
void heap_caps_free(void*);
