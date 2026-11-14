// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_network_diagnostics.hpp"
#include "ug_network_history.hpp"
#include "ug_network_operations.hpp"
#include <cassert>
#include <type_traits>
using namespace ug::embedded;
static_assert(sizeof(NetworkTraceRing) <= 1024);
static_assert(std::is_trivially_copyable<NetworkTraceEvent>::value);
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

  NetworkTraceRing history;
  NetworkTraceEvent event{0,
                          7,
                          3,
                          1,
                          1000,
                          ServiceId::kNtripInput,
                          NtripTraceKind::kFailure,
                          NtripTraceError::kClient,
                          2};
  assert(history.Append(event));
  event.receipt_ns = 9000;
  assert(history.Append(event));
  auto trace = history.Snapshot();
  assert(trace.count == 1 && trace.events[0].receipt_ns == 1000 && trace.coalesced == 1);
  for (unsigned i = 0; i < kNetworkTraceCapacity + 3; ++i)
  {
    ++event.attempt;
    ++event.receipt_ns;
    event.service = i % 2 ? ServiceId::kPublisher4 : ServiceId::kNtripInput;
    assert(history.Append(event));
  }
  trace = history.Snapshot();
  assert(trace.count == kNetworkTraceCapacity && trace.overwritten == 4);
  assert(trace.events[0].sequence == 5 && trace.events.back().sequence == 20);
  assert(trace.events.back().configuration_revision == 7 &&
         trace.events.back().link_generation == 3);
  assert(history.Snapshot().count == trace.count);  // Read is not consumption.
  ++event.configuration_revision;
  assert(history.Append(event));  // New configuration is a distinct event.
  ++event.link_generation;
  assert(history.Append(event));
  for (unsigned i = 0; i < 8; ++i)
  {
    auto bad = event;
    if (i == 0)
      bad.service = ServiceId::kGnss;
    if (i == 1)
      bad.kind = static_cast<NtripTraceKind>(255);
    if (i == 2)
      bad.error = static_cast<NtripTraceError>(255);
    if (i == 3)
      bad.receipt_ns = -1;
    if (i == 4)
      bad.client_error = 255;
    if (i == 5)
      bad.error = NtripTraceError::kNone;
    if (i == 6)
      bad.client_error = 0;
    if (i == 7)
      bad.kind = NtripTraceKind::kEnabled;
    assert(!history.Append(bad));
  }
  assert(history.Snapshot().rejected == 8);
}
