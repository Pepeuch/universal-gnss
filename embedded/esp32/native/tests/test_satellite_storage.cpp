// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_satellite_reports.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <type_traits>
namespace
{
bool fail;
unsigned calls{}, frees{};
std::size_t owned{};
}  // namespace
namespace ug::embedded
{
void* AllocateSatelliteMemory(std::size_t size)
{
  ++calls;
  if (fail)
    return nullptr;
  owned = size;
  auto* p = std::malloc(size + 16);
  assert(p);
  std::memset(static_cast<char*>(p) + size, 0x5a, 16);
  return p;
}
void FreeSatelliteMemory(void* p)
{
  for (std::size_t i = owned; i < owned + 16; ++i)
    assert(static_cast<unsigned char*>(p)[i] == 0x5a);
  ++frees;
  std::free(p);
}
}  // namespace ug::embedded
int main()
{
  using namespace ug::embedded;
  static_assert(!std::is_copy_constructible_v<SatelliteReports>);
  static_assert(sizeof(SatelliteReports) <= 2 * sizeof(void*));
  universal_gnss_protocols::UbxNavSatRecord record;
  record.timestamp_ns = 1;
  record.i_tow_ms = 10;
  record.num_svs = 1;
  record.satellite_count = 1;
  record.satellites[0].sv_id = 7;
  {
    SatelliteReports failed;
    assert(calls == 0 && !failed.Latest(1).receipt_ns);
    failed.Reset();
    assert(calls == 0);
    fail = true;
    assert(!failed.InitializeStorage());
    assert(calls == 1);
    failed.Observe(record);
    assert(calls == 1 && !failed.Latest(1).receipt_ns);
    fail = false;
    assert(!failed.InitializeStorage());  // Failure latched until subsystem recreation.
  }
  assert(frees == 0);
  {
    SatelliteReports reports;
    assert(reports.InitializeStorage() && calls == 2 && owned > 8192);
    const auto* empty = &reports.Latest(1);
    assert(reports.InitializeStorage() && calls == 2 && &reports.Latest(1) == empty);
    reports.Observe(record);
    assert(calls == 2 && reports.Latest(1).entries[0].id == 7);
    reports.Reset();
    assert(!reports.Latest(1).receipt_ns && calls == 2);
    record.timestamp_ns = 5;
    reports.Observe(record);
    assert(reports.Latest(5).receipt_ns == 5);
    assert(!SatelliteReportFresh(reports.Latest(3000000006LL), 3000000006LL));
  }
  assert(frees == 1);
}
