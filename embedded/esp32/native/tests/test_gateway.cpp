// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_gateway.hpp"
#include "ug_observation.hpp"
#include "ug_rtcm.h"
#include "ug_wait.hpp"
#include <cassert>
#include <vector>

using ug::embedded::Gateway;
static std::vector<std::uint8_t> MakeFrame(std::size_t payload)
{
  std::vector<std::uint8_t> bytes(payload + 6, 0);
  bytes[0] = 0xd3;
  bytes[1] = static_cast<std::uint8_t>(payload >> 8);
  bytes[2] = static_cast<std::uint8_t>(payload);
  std::uint32_t crc = 0;
  assert(ug_rtcm_crc24q(bytes.data(), bytes.size() - 3, &crc));
  bytes[bytes.size() - 3] = crc >> 16;
  bytes[bytes.size() - 2] = crc >> 8;
  bytes.back() = crc;
  return bytes;
}
int main()
{
  Gateway gateway;
  Gateway::Frame out;
  assert(!gateway.ConfigureSource(6, 1, true));
  assert(!gateway.ConfigureSource(0, 0, true));
  assert(gateway.ConfigureSource(0, 1, true));
  assert(!gateway.ConfigureSource(0, 1, true));  // Epoch cannot be replayed.
  assert(gateway.ConfigureSource(1, 1, true));
  assert(gateway.ConfigureSink(0, true, 1000));
  assert(gateway.ConfigureSink(1, true, 1000));
  assert(!gateway.ConfigureSink(0, true, 0));
  const auto bytes = MakeFrame(1023);
  // Incomplete old-source bytes cannot join a new incarnation or another source.
  assert(gateway.Feed(0, 1, bytes.data(), 10, 1));
  assert(gateway.ConfigureSource(0, 2, true));
  assert(!gateway.Feed(0, 1, bytes.data(), bytes.size(), 1));
  assert(gateway.Feed(0, 2, bytes.data() + 10, bytes.size() - 10, 1));
  assert(!gateway.Pop(0, 1, out));
  assert(gateway.Feed(1, 1, bytes.data(), 10, 2));
  assert(gateway.Feed(0, 2, bytes.data() + 10, bytes.size() - 10, 2));
  assert(!gateway.Pop(0, 2, out));
  assert(gateway.Feed(1, 1, bytes.data() + 10, bytes.size() - 10, 3));
  assert(gateway.Pop(0, 3, out));
  assert(out.source == 1 && out.receipt_ns == 2 && out.size == bytes.size());
  assert(std::vector<std::uint8_t>(out.bytes.begin(), out.bytes.begin() + out.size) == bytes);
  // Replacement invalidates queued frames in every destination.
  assert(gateway.ConfigureSource(1, 2, true));
  assert(!gateway.Pop(1, 3, out));
  // Independent queues: a slow sink drops whole frames and cannot block others.
  for (int i = 0; i < 4; ++i)
  {
    assert(gateway.Feed(1, 2, bytes.data(), bytes.size(), 4 + i));
    assert(gateway.Pop(0, 4 + i, out));
  }
  assert(gateway.metrics().dropped_full == 2);
  assert(!gateway.Pop(1, 2000, out));
  assert(gateway.metrics().dropped_stale == 2);
  auto bad = bytes;
  bad.back() ^= 1;
  assert(gateway.Feed(1, 2, bad.data(), bad.size(), 2001));
  assert(!gateway.Pop(0, 2001, out));
  assert(!gateway.Feed(1, 2, bytes.data(), bytes.size(), 2000));  // Clock rollback.
  assert(!gateway.Feed(1, 2, nullptr, 1, 2001));
  assert(!gateway.Feed(1, 2, bytes.data(), 4097, 2001));
  // Equal numeric coordinates/frames are still separate arrivals with receipt time.
  for (std::size_t size = 0; size <= 1023; ++size)
  {
    const auto frame = MakeFrame(size);
    assert(gateway.Feed(1, 2, frame.data(), frame.size(), 3000 + size));
    assert(gateway.Pop(0, 3000 + size, out));
    assert(out.size == frame.size() && out.receipt_ns == 3000 + static_cast<std::int64_t>(size));
  }
  assert(gateway.ConfigureSource(1, 3, false));
  assert(!gateway.Feed(1, 3, bytes.data(), bytes.size(), 5000));
  universal_gnss::GnssRuntimeState position;
  position.fix_valid = true;
  position.latitude_deg = 48.123456789;
  assert(ug::embedded::FreshPositionForGga(position, 10, 10).fix_valid);
  assert(ug::embedded::FreshPositionForGga(position, 10, 3000000010LL).fix_valid);
  assert(!ug::embedded::FreshPositionForGga(position, 10, 3000000011LL).fix_valid);
  assert(!ug::embedded::FreshPositionForGga(position, 10, 9).latitude_deg);
  assert(!ug::embedded::FreshPositionForGga(position, std::nullopt, 10).fix_valid);
  assert(position.fix_valid && position.latitude_deg);  // Cached source was not rewritten.
}

static_assert(ug::embedded::FiniteWaitTicks(5, 100) == 1);
static_assert(ug::embedded::FiniteWaitTicks(5, 1000) == 5);
static_assert(ug::embedded::FiniteWaitTicks(10, 50) == 1);
static_assert(ug::embedded::FiniteWaitTicks(11, 100) == 2);
static_assert(ug::embedded::FiniteWaitTicks(UINT32_MAX, UINT32_MAX) == UINT32_MAX - 1);
