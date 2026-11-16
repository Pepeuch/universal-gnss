// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
void* heap_caps_malloc(std::size_t, unsigned);
void heap_caps_free(void*);
