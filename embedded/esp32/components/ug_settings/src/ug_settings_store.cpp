// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_settings_store.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include <algorithm>
#include <cstring>
namespace ug::embedded
{
namespace
{
std::uint64_t ReadLe(const std::uint8_t* p, unsigned n)
{
  std::uint64_t value = 0;
  for (unsigned i = 0; i < n; ++i)
    value |= std::uint64_t(p[i]) << (8 * i);
  return value;
}
void WriteLe(std::uint8_t* p, std::uint64_t value, unsigned n)
{
  for (unsigned i = 0; i < n; ++i)
    p[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
}  // namespace
SettingsStore::SettingsStore(SettingsStorage& storage, Capabilities caps)
    : storage_(storage), capabilities_(std::move(caps))
{
}
bool SettingsStore::Load(Settings& out, std::string& error)
{
  loaded_ = false;
  recovered_ = false;
  generation_ = 0;
  active_ = 1;
  Settings candidates[2];
  std::uint64_t generations[2]{};
  unsigned found = 0, valid = 0;
  std::vector<std::uint8_t> records[2];
  for (unsigned slot = 0; slot < 2; ++slot)
  {
    auto& record = records[slot];
    auto result = storage_.Read(slot, record);
    if (result == BlobRead::kError)
    {
      error = "persistent storage read failed";
      return false;
    }
    if (result == BlobRead::kMissing)
      continue;
    ++found;
    if (record.size() < 23 || record.size() > kRecordLimit ||
        std::memcmp(record.data(), "UGCF", 4) != 0)
      continue;
    const auto crc = universal_gnss_protocols::ComputeRtcmCrc24Q(record.data(), record.size() - 3);
    if (ReadLe(record.data() + record.size() - 3, 3) != crc)
      continue;
    // Unknown schemas must never be silently overwritten by older firmware.
    if (ReadLe(record.data() + 4, 4) != Settings::kSchema)
    {
      error = "persistent schema requires newer firmware or explicit migration";
      return false;
    }
    if (ReadLe(record.data() + 16, 4) != record.size() - 23)
      continue;
    const auto generation = ReadLe(record.data() + 8, 8);
    if (!generation)
      continue;
    const std::string_view payload(reinterpret_cast<const char*>(record.data() + 20),
                                   record.size() - 23);
    if (payload.find("{\"schema\":1,") != 0)
      continue;  // Canonical complete snapshots, never partial patches.
    if (!DecodeSettings(
            payload, DefaultSettings(capabilities_), capabilities_, candidates[slot], error))
      continue;
    generations[slot] = generation;
    ++valid;
  }
  if (found && !valid)
  {
    error = "no valid persistent configuration; originals preserved";
    return false;
  }
  if (valid == 2 && generations[0] == generations[1] && records[0] != records[1])
  {
    error = "ambiguous persistent configuration generations";
    return false;
  }
  if (valid)
  {
    active_ = generations[0] >= generations[1] ? 0 : 1;
    generation_ = generations[active_];
    out = std::move(candidates[active_]);
    out.revision = 1;
  }
  else
    out = DefaultSettings(capabilities_);
  recovered_ = valid < found;
  loaded_ = true;
  error.clear();
  return true;
}
bool SettingsStore::Save(const Settings& s, std::string& error)
{
  if (!loaded_ || generation_ == UINT64_MAX)
  {
    error = "storage not loaded or generation exhausted";
    return false;
  }
  if (!ValidateSettings(s, capabilities_, error))
    return false;
  std::string json;
  if (!EncodeSettings(s, json, true))
  {
    error = "configuration exceeds persistence budget";
    return false;
  }
  std::vector<std::uint8_t> record(json.size() + 23, 0);
  std::memcpy(record.data(), "UGCF", 4);
  WriteLe(record.data() + 4, Settings::kSchema, 4);
  WriteLe(record.data() + 8, generation_ + 1, 8);
  WriteLe(record.data() + 16, json.size(), 4);
  std::memcpy(record.data() + 20, json.data(), json.size());
  WriteLe(record.data() + record.size() - 3,
          universal_gnss_protocols::ComputeRtcmCrc24Q(record.data(), record.size() - 3),
          3);
  const unsigned target = 1 - active_;
  if (!storage_.Write(target, record))
  {
    error = "persistent write failed; reload required";
    loaded_ = false;
    return false;
  }
  // Verify committed bytes before accepting the new snapshot. The other bank remains untouched.
  std::vector<std::uint8_t> check;
  if (storage_.Read(target, check) != BlobRead::kFound || check != record)
  {
    error = "persistent verification failed; reload required";
    loaded_ = false;
    return false;
  }
  active_ = target;
  ++generation_;
  recovered_ = false;
  error.clear();
  return true;
}
}  // namespace ug::embedded
