// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_configuration.hpp"
#include "nvs.h"
#include "ug_settings_store.hpp"
#include <atomic>
#include <mutex>
namespace ug::embedded
{
namespace
{
class NvsStorage final : public SettingsStorage
{
public:
  bool Open()
  {
    return nvs_open("ug_config", NVS_READWRITE, &handle) == ESP_OK;
  }
  BlobRead Read(unsigned slot, std::vector<std::uint8_t>& out) override
  {
    out.clear();
    const auto* key = slot == 0 ? "cfg0" : "cfg1";
    size_t size = 0;
    const auto error = nvs_get_blob(handle, key, nullptr, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND)
      return BlobRead::kMissing;
    if (error != ESP_OK)
      return BlobRead::kError;
    if (size > SettingsStore::kRecordLimit)
      return BlobRead::kFound;  // No unbounded allocation.
    out.resize(size);
    if (!size)
      return BlobRead::kFound;
    return nvs_get_blob(handle, key, out.data(), &size) == ESP_OK ? BlobRead::kFound
                                                                  : BlobRead::kError;
  }
  bool Write(unsigned slot, const std::vector<std::uint8_t>& bytes) override
  {
    return bytes.size() <= SettingsStore::kRecordLimit &&
           nvs_set_blob(handle, slot == 0 ? "cfg0" : "cfg1", bytes.data(), bytes.size()) ==
               ESP_OK &&
           nvs_commit(handle) == ESP_OK;
  }
  nvs_handle_t handle{};
};
NvsStorage storage;
Capabilities capabilities;
std::unique_ptr<SettingsStore> store;
std::shared_ptr<const Settings> snapshot;
std::mutex writer;
std::atomic<bool> writable{false}, journal_blocked{true};
}  // namespace
bool StartConfiguration(Capabilities caps, std::string& error)
{
  std::lock_guard<std::mutex> lock(writer);
  if (snapshot)
  {
    error = "configuration already initialized";
    return false;
  }
  capabilities = std::move(caps);
  Settings s = DefaultSettings(capabilities);
  bool ok = storage.Open();
  if (ok)
  {
    store = std::make_unique<SettingsStore>(storage, capabilities);
    ok = store->Load(s, error);
    std::uint8_t marker = 1;
    auto result = nvs_get_u8(storage.handle, "gnss_dirty", &marker);
    journal_blocked = (result == ESP_ERR_NVS_NOT_FOUND) ? false : (result != ESP_OK || marker != 0);
  }
  else
    error = "NVS unavailable; defaults only, no automatic erase";
  std::atomic_store(&snapshot, std::make_shared<const Settings>(std::move(s)));
  writable = ok;
  return ok;
}
std::shared_ptr<const Settings> Configuration()
{
  return std::atomic_load(&snapshot);
}
const Capabilities& BoardCapabilities()
{
  return capabilities;
}
bool ConfigurationWritable()
{
  return writable;
}
bool PatchConfiguration(std::string_view json, std::string& error)
{
  std::lock_guard<std::mutex> lock(writer);
  if (!writable || !store)
  {
    error = "persistent configuration locked";
    return false;
  }
  auto previous = Configuration();
  Settings next;
  if (!previous || !DecodeSettings(json, *previous, capabilities, next, error))
    return false;
  // Allocate the immutable publication before changing persistent state.
  auto replacement = std::make_shared<const Settings>(next);
  if (!store->Save(next, error))
  {
    writable = false;
    return false;
  }
  std::atomic_store(&snapshot, std::move(replacement));
  return true;
}
bool ReceiverJournalBlocked()
{
  return journal_blocked;
}
bool BeginReceiverJournal()
{
  std::lock_guard<std::mutex> lock(writer);
  if (!writable || journal_blocked)
    return false;
  journal_blocked = true;  // Failure is deliberately fail-closed in this boot too.
  return nvs_set_u8(storage.handle, "gnss_dirty", 1) == ESP_OK &&
         nvs_commit(storage.handle) == ESP_OK;
}
bool CompleteReceiverJournal()
{
  std::lock_guard<std::mutex> lock(writer);
  if (!writable || !journal_blocked)
    return false;
  if (nvs_set_u8(storage.handle, "gnss_dirty", 0) != ESP_OK || nvs_commit(storage.handle) != ESP_OK)
    return false;
  journal_blocked = false;
  return true;
}
}  // namespace ug::embedded
