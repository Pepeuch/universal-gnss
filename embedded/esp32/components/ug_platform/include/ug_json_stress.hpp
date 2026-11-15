// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_json.hpp"
namespace ug::embedded
{
// Pure synthetic document, never a receiver observation or saved configuration.
// Only 0/16/64 rows; caller owns all allocations and must check failure count.
inline JsonDocument BuildMemoryJsonFixture(unsigned count)
{
  JsonDocument root(nullptr, cJSON_Delete);
  if (count != 0 && count != 16 && count != 64)
    return root;
  root.reset(cJSON_CreateObject());
  if (!root || !cJSON_AddStringToObject(root.get(), "scope", "synthetic_memory_only"))
    return {nullptr, cJSON_Delete};
  auto* rows = cJSON_AddArrayToObject(root.get(), "rows");
  if (!rows)
    return {nullptr, cJSON_Delete};
  for (unsigned i = 0; i < count; ++i)
  {
    auto* row = cJSON_CreateObject();
    if (!row)
      return {nullptr, cJSON_Delete};
    if (!cJSON_AddItemToArray(rows, row))
    {
      cJSON_Delete(row);
      return {nullptr, cJSON_Delete};
    }
    for (unsigned j = 0; j < 8; ++j)
    {
      const char key[]{static_cast<char>('a' + j), 0};
      if (!cJSON_AddNumberToObject(row, key, 1.23456789012345 + i * 10 + j))
        return {nullptr, cJSON_Delete};
    }
    if (!cJSON_AddStringToObject(row, "text", "synthetic \"escaped\" line\nUTF-8 \xe2\x98\x83"))
      return {nullptr, cJSON_Delete};
  }
  return root;
}
}  // namespace ug::embedded
