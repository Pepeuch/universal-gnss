// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_uart_drain.hpp"
#include <algorithm>
#include <cassert>
#include <numeric>
#include <vector>
using namespace universal_gnss_transport;
int main()
{
  std::vector<std::uint8_t> input(9000), output;
  std::iota(input.begin(), input.end(), 0);
  std::size_t offset = 0, calls = 0;
  auto read = [&](auto* data, auto capacity, bool wait) {
    assert(wait == (calls++ == 0));
    const auto size = std::min(capacity, input.size() - offset);
    std::copy_n(input.data() + offset, size, data);
    offset += size;
    return ReadResult{size};
  };
  auto consume = [&](const auto* data, auto size) {
    output.insert(output.end(), data, data + size);
  };
  // Large continuous input returns at the turn budget, allowing a scheduler yield.
  auto first = ug::embedded::DrainUart(read, consume);
  assert(!first.failed && first.budget_exhausted && first.bytes == 4096 && first.batches == 8);
  calls = 0;
  auto second = ug::embedded::DrainUart(read, consume);
  assert(!second.failed && second.budget_exhausted && second.bytes == 4096);
  calls = 0;
  auto third = ug::embedded::DrainUart(read, consume);
  assert(!third.failed && !third.budget_exhausted && third.bytes == 808 && calls == 3);
  assert(output == input);  // Byte order preserved across yield boundaries.
  calls = 0;
  auto empty = ug::embedded::DrainUart(read, consume);
  assert(empty.bytes == 0 && calls == 1 && !empty.failed);
  calls = 0;
  auto failed = ug::embedded::DrainUart(
      [&](auto*, auto, bool) {
        ++calls;
        return ReadResult{3, TransportStatus::kError, TransportError::kReadFailure};
      },
      [](auto*, auto) { assert(false); });
  assert(failed.failed && failed.bytes == 0 && calls == 1);
  auto oversized =
      ug::embedded::DrainUart([](auto*, auto capacity, bool) { return ReadResult{capacity + 1}; },
                              [](auto*, auto) { assert(false); });
  assert(oversized.failed && oversized.bytes == 0);
}
