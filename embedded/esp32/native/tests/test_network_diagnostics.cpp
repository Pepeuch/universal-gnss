// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_network_diagnostics.hpp"
#include "ug_network_operations.hpp"
#include <cassert>
using namespace ug::embedded;
int main()
{
  NetworkServiceStats raw;
  raw.upstream = {7, 3, 1000, 10, 100, 2, 0, 0, true, false, true};
  raw.publishers[0] = raw.caster = raw.upstream;
  raw.upstream_flowing = true;
  auto status = ProjectNetworkStats(raw, 7, 3, true, 1000);
  assert(status.upstream.active && status.upstream_flowing && status.publishers[0].active &&
         status.caster.active);
  assert(!status.publishers[1].current && !status.publishers[1].active);
  assert(raw.upstream.active && !raw.upstream.current);  // Projection never mutates owner record.
  assert(ProjectNetworkStats(raw, 7, 3, true, 5000001000LL).upstream.current);
  for (auto stale : {ProjectNetworkStats(raw, 7, 3, true, 5000001001LL),
                     ProjectNetworkStats(raw, 8, 3, true, 1000),
                     ProjectNetworkStats(raw, 7, 4, true, 1000),
                     ProjectNetworkStats(raw, 7, 3, false, 1000),
                     ProjectNetworkStats(raw, 7, 3, true, 999)})
  {
    assert(!stale.upstream.current && !stale.upstream.active && !stale.upstream_flowing);
    assert(!stale.caster.active && !stale.publishers[0].active);
    assert(stale.upstream.frames == 10 &&
           stale.upstream.bytes == 100);  // Diagnostic history retained.
  }
  raw.upstream.receipt_ns = -1;
  assert(!ProjectNetworkStats(raw, 7, 3, true, INT64_MAX).upstream.current);
  raw.upstream.sampled = false;
  assert(!ProjectNetworkStats(raw, 7, 3, true, 1000).upstream.current);
  std::string error;
  assert(DecodeNetworkRestart(R"({"confirm":"RESTART_NTRIP_SERVICES"})", error));
  for (const auto* invalid : {"{}",
                              "[]",
                              R"({"confirm":"RESET"})",
                              R"({"confirm":true})",
                              R"({"confirm":"RESTART_NTRIP_SERVICES","slot":0})",
                              R"({"confirm":"RESTART_NTRIP_SERVICES","confirm":"RESET"})"})
    assert(!DecodeNetworkRestart(invalid, error));
}
