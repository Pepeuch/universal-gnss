// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_wire_selftest.hpp"
#include <cassert>
#include <cstring>
#include <string>
using namespace ug::embedded;
int main()
{
  assert(DecodeWireSelfTest(R"({"confirm":"RUN_OFFLINE_WIRE_SELFTEST"})"));
  for (const char* body :
       {"",
        "{}",
        "[]",
        "null",
        R"({"confirm":true})",
        R"({"confirm":"RUN_OFFLINE_WIRE_SELFTEST","unexpected":1})",
        R"({"confirm":"RUN_OFFLINE_WIRE_SELFTEST","confirm":"RUN_OFFLINE_WIRE_SELFTEST"})",
        R"({"confirm":"RUN_OFFLINE_WIRE_SELFTEST\u0000ignored"})",
        R"({"confirm":"run_offline_wire_selftest"})"})
    assert(!DecodeWireSelfTest(body));
  assert(!DecodeWireSelfTest(std::string(8193, ' ')));
  assert(!WireSelfTestReport{}.ok());
  assert((!WireSelfTestReport{kWireSelfTestMask ^ 1u, 0}.ok()));
  assert((!WireSelfTestReport{kWireSelfTestMask, 1}.ok()));
  for (unsigned i = 0; i < 32; ++i)
  {
    const auto report = RunWireSelfTest();
    assert(report.ok() && report.passed == kWireSelfTestMask && report.failed == 0);
  }
  for (unsigned i = 0; i < kWireSelfTestCount; ++i)
    assert(std::strcmp(WireSelfTestName(i), "invalid") != 0);
  assert(std::strcmp(WireSelfTestName(kWireSelfTestCount), "invalid") == 0);
}
