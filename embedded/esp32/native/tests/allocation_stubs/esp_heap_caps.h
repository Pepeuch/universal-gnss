// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_DEFAULT = 4;
void* heap_caps_aligned_alloc(std::size_t, std::size_t, unsigned);
void* heap_caps_malloc(std::size_t, unsigned);
void heap_caps_free(void*);
std::size_t heap_caps_get_allocated_size(void*);
