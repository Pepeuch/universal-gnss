// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_http_encoding.hpp"
#include <cassert>
#include <initializer_list>
using namespace ug::embedded;
int main()
{
  assert(AcceptsGzip({}, false));
  for (const auto* header : {"gzip",
                             "GZip",
                             "gzip, deflate, br",
                             "*",
                             "*;q=0.1",
                             "gzip;q=1",
                             "gzip;q=1.000",
                             "gzip;q=0.001",
                             "gzip;q=1.",
                             "GZIP ; Q = 0.3",
                             "\t gzip \t, br",
                             "*;q=0, gzip;q=0.5"})
    assert(AcceptsGzip(header, true));
  for (const auto* header : {"",          "identity",     "br, deflate",    "*;q=0",
                             "gzip;q=0",  "gzip;q=0.000", "gzip;q=1.001",   "gzip;q=0.0001",
                             "gzip;q=-1", "gzip;q=.5",    "gzip;q=nan",     "gzip;level=9",
                             "gzip;q=",   "gzip;q=1;q=1", "gzip;q=0,*;q=1", "gzip;q=0,gzip;q=1",
                             "gzip;q=00", "gzip;q=2",     "gzip-extra",     "gzip;q=0.1x"})
    assert(!AcceptsGzip(header, true));
}
