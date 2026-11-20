// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
struct MockHeap;
using multi_heap_handle_t = MockHeap*;
struct multi_heap_info_t
{
  std::size_t total_free_bytes{}, total_allocated_bytes{}, largest_free_block{},
      minimum_free_bytes{};
};
multi_heap_handle_t multi_heap_register(void*, std::size_t);
void multi_heap_set_lock(multi_heap_handle_t, void*);
void* multi_heap_aligned_alloc_offs(multi_heap_handle_t, std::size_t, std::size_t, std::size_t);
void multi_heap_free(multi_heap_handle_t, void*);
void multi_heap_get_info(multi_heap_handle_t, multi_heap_info_t*);
