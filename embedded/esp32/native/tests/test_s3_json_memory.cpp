// SPDX-License-Identifier: LGPL-3.0-only
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "ug_json_stress.hpp"
#include "ug_s3_json_memory.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
namespace
{
std::unordered_map<void*, std::size_t> live;
int budget = -1;
unsigned calls;
cJSON* Document()
{
  auto* root = cJSON_CreateObject();
  if (root)
  {
    cJSON_AddNumberToObject(root, "latitude_deg", 48.123456789012345);
    cJSON_AddStringToObject(root, "text", "quote\" line\n UTF-8: \xe2\x98\x83");
    cJSON_AddStringToObject(root, "sequence", "18446744073709551615");
    cJSON_AddStringToObject(root, "secret_fixture", "synthetic-sensitive-buffer");
  }
  return root;
}
}  // namespace
void* heap_caps_malloc(std::size_t size, std::uint32_t caps)
{
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ++calls;
  if (budget == 0)
    return nullptr;
  if (budget > 0)
    --budget;
  auto* data = std::malloc(size + 16);
  if (data)
  {
    live[data] = size;
    std::memset(static_cast<char*>(data) + size, 0x5a, 16);
  }
  return data;
}
void heap_caps_free(void* data)
{
  const auto size = live.at(data);
  for (std::size_t i = 0; i < size; ++i)
    assert(static_cast<char*>(data)[i] == 0);
  for (std::size_t i = size; i < size + 16; ++i)
    assert(static_cast<unsigned char*>(data)[i] == 0x5a);
  live.erase(data);
  std::free(data);
}
int main()
{
  auto* root = Document();
  auto* printed = cJSON_PrintUnformatted(root);
  const std::string reference(printed);
  cJSON_free(printed);
  cJSON_Delete(root);
  ug::embedded::InitializeS3JsonMemory();
  assert(cJSON_malloc(std::numeric_limits<std::size_t>::max()) == nullptr);
  root = Document();
  printed = cJSON_PrintUnformatted(root);
  assert(printed && reference == printed);
  cJSON_free(printed);
  cJSON_Delete(root);
  assert(live.empty() && calls > 0);
  // Every allocation boundary may fail; no internal retry, leak or partial wipe.
  for (int i = 0; i < 40; ++i)
  {
    budget = i;
    root = Document();
    printed = cJSON_PrintUnformatted(root);
    cJSON_free(printed);
    cJSON_Delete(root);
    assert(live.empty());
  }
  budget = -1;
  for (unsigned count : {0U, 16U, 64U})
  {
    auto fixture = ug::embedded::BuildMemoryJsonFixture(count);
    assert(fixture);
    auto* text = cJSON_PrintUnformatted(fixture.get());
    assert(text && std::strlen(text) < 32768);
    if (count == 64)
      assert(std::strlen(text) > 8192);
    auto* parsed = cJSON_Parse(text);
    assert(parsed && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(parsed, "rows")) ==
                         static_cast<int>(count));
    cJSON_Delete(parsed);
    cJSON_free(text);
    fixture.reset();
    assert(live.empty());
  }
  const auto failed_before = ug::embedded::S3JsonMemoryFailures();
  assert(ug::embedded::ArmS3JsonMemoryFailure(3));
  root = Document();
  printed = cJSON_PrintUnformatted(root);
  assert(ug::embedded::S3JsonMemoryFailures() == failed_before + 1);
  cJSON_free(printed);
  cJSON_Delete(root);
  assert(live.empty());
  root = Document();
  printed = cJSON_PrintUnformatted(root);
  assert(printed && reference == printed);
  cJSON_free(printed);
  cJSON_Delete(root);
  assert(live.empty());
  assert(!ug::embedded::ArmS3JsonMemoryFailure(4097));
  assert(ug::embedded::ArmS3JsonMemoryFailure(-1));
  assert(ug::embedded::ArmS3JsonMemoryFailure(200));
  auto rejected_large = ug::embedded::BuildMemoryJsonFixture(64);
  assert(!rejected_large && live.empty());
  assert(ug::embedded::ArmS3JsonMemoryFailure(-1));
}
