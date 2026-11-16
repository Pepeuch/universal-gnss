// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_pairing_read.hpp"
#include <cassert>
#include <string_view>
using namespace ug::embedded;
int main()
{
  GatewayPairingRead state;
  assert(!state.Query("UGCFG/1 GET", 0));
  assert(state.Query("UGCFG/1 OPEN 0123456789abcdef", 50));
  assert(std::string_view(state.nonce()) == "0123456789abcdef");
  for (auto malformed : {"UGCFG/1 OPEN 0123456789abcdeg",
                         "UGCFG/1 OPEN 0123456789abcde",
                         "UGCFG/1 OPEN 0123456789abcdef ",
                         "UGCFG/1 GET extra",
                         "UGCFG/1 CLEAR CONFIRM_UNPAIR",
                         "reboot CONFIRM"})
    assert(!state.Query(malformed, 100));
  assert(std::string_view(state.nonce()) == "0123456789abcdef");
  assert(!state.Query("UGCFG/1 GET", 49));  // Backward/unrelated time is invalid.
  assert(state.Query("UGCFG/1 GET", 120000049));
  assert(!state.Query("UGCFG/1 GET", 120000050));
  assert(!state.Query("UGCFG/1 OPEN 0000000000000000", -1));
  assert(!state.Query("UGCFG/1 OPEN 0000000000000000", INT64_MAX));
  assert(!state.Query("UGCFG/1 GET", 120000051));  // Invalid opens never renew it.
  assert(state.Query("UGCFG/1 OPEN fedcba9876543210", 200000000));
  assert(state.Query("UGCFG/1 GET", 200000001));
  assert(std::string_view(state.nonce()) == "fedcba9876543210");
}
