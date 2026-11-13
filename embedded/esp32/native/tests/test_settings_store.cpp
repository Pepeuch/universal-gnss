// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_settings_store.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include <cassert>
using namespace ug::embedded;
struct Memory final : SettingsStorage
{
  std::vector<std::uint8_t> banks[2];
  bool fail = false, read_error = false, torn = false;
  BlobRead Read(unsigned slot, std::vector<std::uint8_t>& out) override
  {
    if (read_error)
      return BlobRead::kError;
    out = banks[slot];
    return out.empty() ? BlobRead::kMissing : BlobRead::kFound;
  }
  bool Write(unsigned slot, const std::vector<std::uint8_t>& in) override
  {
    if (fail)
      return false;
    banks[slot] = in;
    if (torn)
    {
      banks[slot].resize(50);
      return false;
    }
    return true;
  }
};
int main()
{
  Capabilities caps{true, true, false, 5, "s3", "gateway"};
  Memory mem;
  SettingsStore store(mem, caps);
  Settings out;
  std::string error;
  assert(!store.Save(DefaultSettings(caps), error));
  assert(store.Load(out, error) && out.device_name == "ug-rtk" && store.generation() == 0);
  out.device_name = "first";
  assert(store.Save(out, error));
  assert(store.generation() == 1 && !mem.banks[0].empty() && mem.banks[1].empty());
  out.device_name = "second";
  assert(store.Save(out, error));
  assert(store.generation() == 2 && !mem.banks[1].empty());
  SettingsStore restart(mem, caps);
  assert(restart.Load(out, error) && out.device_name == "second");
  mem.fail = true;
  out.device_name = "not-saved";
  assert(!restart.Save(out, error));
  mem.fail = false;
  assert(!restart.Save(out, error));  // Reload required after ambiguous write.
  assert(restart.Load(out, error) && out.device_name == "second");
  // Power interruption/torn inactive bank: old committed configuration remains usable.
  mem.torn = true;
  out.device_name = "torn";
  assert(!restart.Save(out, error));
  mem.torn = false;
  SettingsStore recovery(mem, caps);
  assert(recovery.Load(out, error));
  assert(recovery.recovered() && out.device_name == "second");
  out.device_name = "third";
  assert(recovery.Save(out, error));
  // Checksum failure falls back to the intact older bank, without erasing either.
  mem.banks[0].back() ^= 1;
  SettingsStore checksum(mem, caps);
  assert(checksum.Load(out, error));
  assert(checksum.recovered() && out.device_name == "second");
  mem.banks[1].back() ^= 1;
  SettingsStore both_bad(mem, caps);
  assert(!both_bad.Load(out, error));
  assert(!both_bad.Save(DefaultSettings(caps), error));
  mem = {};
  SettingsStore fresh(mem, caps);
  assert(fresh.Load(out, error));
  assert(fresh.Save(out, error));
  auto& rec = mem.banks[0];
  rec[4] = 2;
  auto crc = universal_gnss_protocols::ComputeRtcmCrc24Q(rec.data(), rec.size() - 3);
  for (unsigned i = 0; i < 3; ++i)
    rec[rec.size() - 3 + i] = crc >> (i * 8);
  SettingsStore future(mem, caps);
  assert(!future.Load(out, error));
  assert(!future.Save(out, error));
  mem.read_error = true;
  SettingsStore io(mem, caps);
  assert(!io.Load(out, error));
}
