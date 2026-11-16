// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_rtcm_rules.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
using ug::embedded::RtcmRules;
int main()
{
  RtcmRules a, b;
  std::string error;
  assert(a.Configure("1004,1005(10),1006(10),1077,1087,1097,1127,1230(10)", error));
  assert(b.Configure("1005(20),1087", error));
  assert(a.Allows(1005, 0));
  a.Sent(1005, 0);
  assert(!a.Allows(1005, 9999999999LL));
  assert(a.Allows(1005, 10000000000LL));
  assert(b.Allows(1005, 1));
  b.Sent(1005, 1);
  assert(!b.Allows(1005, 10000000000LL));
  assert(b.Allows(1005, 20000000001LL));
  assert(!a.Allows(1019, 1));
  assert(a.Allows(1077, 1));
  a.Sent(1077, 1);
  assert(a.Allows(1077, 1));
  assert(!b.Allows(1077, 1));
  assert(b.Allows(1087, 1));
  for (const auto* bad : {"0",
                          "4096",
                          "1005(-1)",
                          "1005(1.5)",
                          "1005(86401)",
                          "1005,1005",
                          "1005,",
                          ",1005",
                          "1005()",
                          "1005(1)junk",
                          "1005(1)(2)"})
    assert(!a.Configure(bad, error) && !error.empty());
  assert(!a.Allows(1019, 1));  // Failed parser never modifies active policy.
  assert(a.Configure(" 1005(0), 1077 ", error));
  assert(a.Allows(1005, 0));
  assert(a.Configure("", error));
  assert(a.Allows(1019, 0));
  assert(!a.Allows(1019, -1));
  std::string full;
  for (unsigned i = 1; i <= 32; ++i)
    full += (i == 1 ? "" : ",") + std::to_string(i);
  assert(a.Configure(full, error));
  assert(!a.Configure(full + ",33", error));
  auto start = std::chrono::steady_clock::now();
  unsigned allowed = 0;
  for (unsigned i = 0; i < 1000000; ++i)
    allowed += a.Allows(32, i);
  assert(allowed == 1000000);
  auto ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
          .count();
  std::printf(
      "Host-only 32-rule lookup: %lld ns/call; rules state %zu bytes; no frame allocation\\n",
      (long long)(ns / 1000000),
      sizeof(a));
}
