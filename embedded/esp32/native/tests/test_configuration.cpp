// SPDX-License-Identifier: LGPL-3.0-only
#include "nvs.h"
#include "ug_configuration.hpp"
#include <cassert>
#include <cstring>
#include <map>
#include <vector>
static std::map<std::string, std::vector<std::uint8_t>> records, pending;
static bool open_fail = false, read_fail = false, commit_fail = false, marker_corrupt = false;
esp_err_t nvs_open(const char* name, int, nvs_handle_t* handle)
{
  assert(std::strcmp(name, "ug_config") == 0);
  *handle = 1;
  return open_fail ? -1 : ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* data, std::size_t* size)
{
  if (read_fail)
    return -1;
  auto found = records.find(key);
  if (found == records.end())
    return ESP_ERR_NVS_NOT_FOUND;
  if (!data)
  {
    *size = found->second.size();
    return ESP_OK;
  }
  if (*size < found->second.size())
    return -1;
  std::memcpy(data, found->second.data(), found->second.size());
  *size = found->second.size();
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* bytes, std::size_t size)
{
  auto* data = static_cast<const std::uint8_t*>(bytes);
  pending[key] = {data, data + size};
  return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t, const char* key, std::uint8_t* out)
{
  if (marker_corrupt)
    return -1;
  auto it = records.find(key);
  if (it == records.end())
    return ESP_ERR_NVS_NOT_FOUND;
  *out = it->second[0];
  return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t, const char* key, std::uint8_t value)
{
  pending[key] = {value};
  return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t)
{
  if (commit_fail)
    return -1;
  for (auto& pair : pending)
    records[pair.first] = pair.second;
  pending.clear();
  return ESP_OK;
}
void nvs_close(nvs_handle_t)
{
}
int main(int argc, char** argv)
{
  assert(argc == 2);
  const std::string mode = argv[1];
  open_fail = mode == "open-failure";
  read_fail = mode == "read-failure";
  marker_corrupt = mode == "journal-corrupt";
  if (mode == "corrupt")
    records["cfg0"] = {1, 2, 3};
  if (mode == "interrupted")
    records["gnss_dirty"] = {1};
  ug::embedded::Capabilities caps{true, true, false, 5, "test-s3", "gateway"};
  std::string error;
  const bool expected = !open_fail && !read_fail && mode != "corrupt";
  assert(ug::embedded::StartConfiguration(caps, error) == expected);
  auto snapshot = ug::embedded::Configuration();
  assert(snapshot && snapshot->device_name == "ug-rtk");
  assert(ug::embedded::ConfigurationWritable() == expected);
  if (expected)
  {
    assert(ug::embedded::PatchConfiguration(R"({"name":"updated"})", error));
    assert(snapshot->device_name == "ug-rtk");  // Immutable old readers remain valid.
    assert(ug::embedded::Configuration()->device_name == "updated");
    if (mode == "interrupted" || mode == "journal-corrupt")
    {
      assert(ug::embedded::ReceiverJournalBlocked());
      assert(!ug::embedded::BeginReceiverJournal());
    }
    else
    {
      assert(!ug::embedded::ReceiverJournalBlocked());
      assert(ug::embedded::BeginReceiverJournal());
      assert(records["gnss_dirty"] == std::vector<std::uint8_t>{1});
      assert(!ug::embedded::BeginReceiverJournal());
      assert(ug::embedded::CompleteReceiverJournal());
      assert(!ug::embedded::ReceiverJournalBlocked());
      commit_fail = true;
      assert(!ug::embedded::BeginReceiverJournal());
      assert(ug::embedded::ReceiverJournalBlocked());
    }
    commit_fail = true;
    assert(!ug::embedded::PatchConfiguration(R"({"name":"unsaved"})", error));
    assert(ug::embedded::Configuration()->device_name == "updated");
    assert(!ug::embedded::ConfigurationWritable());
  }
  else
  {
    assert(!ug::embedded::PatchConfiguration(R"({"name":"blocked"})", error));
    assert(!ug::embedded::BeginReceiverJournal());
  }
  assert(!ug::embedded::StartConfiguration(caps, error));
}
