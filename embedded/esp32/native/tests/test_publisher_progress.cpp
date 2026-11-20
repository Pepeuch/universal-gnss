// SPDX-License-Identifier: LGPL-3.0-only
// Deterministic socket seam: exact production publisher, bounded partial writes.
#include "ug_ntrip_publisher.hpp"
#include "ug_rtcm.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>
namespace
{
std::vector<std::uint8_t> output;
std::size_t chunk = 4096;
bool wait_write{}, break_write{}, acknowledged{};
unsigned writes{};
}  // namespace
namespace universal_gnss_transport
{
TcpClientTransport::TcpClientTransport(const TcpClientConfig& config) : config_(config)
{
}
TcpClientTransport::~TcpClientTransport()
{
  Close();
}
TransportError TcpClientTransport::Open(const TcpClientConfig& config)
{
  config_ = config;
  fd_ = 1;
  acknowledged = false;
  return TransportError::kNone;
}
void TcpClientTransport::Close()
{
  fd_ = -1;
}
bool TcpClientTransport::IsOpen() const
{
  return fd_ >= 0;
}
ReadResult TcpClientTransport::Read(std::uint8_t* data, std::size_t size)
{
  if (acknowledged)
    return {};
  constexpr char reply[] = "ICY 200 OK\r\n";
  assert(size >= sizeof(reply) - 1);
  std::memcpy(data, reply, sizeof(reply) - 1);
  acknowledged = true;
  return {sizeof(reply) - 1, TransportStatus::kOk, TransportError::kNone};
}
WriteResult TcpClientTransport::Write(const std::uint8_t* data, std::size_t size)
{
  ++writes;
  if (break_write)
    return {0, TransportStatus::kError, TransportError::kWriteFailure};
  if (wait_write)
    return {0, TransportStatus::kError, TransportError::kTimeout};
  size = std::min(size, chunk);
  output.insert(output.end(), data, data + size);
  return {size, TransportStatus::kOk, TransportError::kNone};
}
}  // namespace universal_gnss_transport
int main()
{
  using namespace ug::embedded;
  std::int64_t now = 10000000000LL;
  universal_gnss_ntrip::NtripConfig config;
  config.host = "fixture.invalid";
  config.port = 2101;
  config.mountpoint = "TEST";
  config.password = "test-only";
  NtripPublisher publisher;
  assert(publisher.Configure(config, true, {}, "1005(10)", true));
  assert(publisher.BlockingStepDue(now));
  publisher.Tick(now);
  assert(publisher.BlockingStepDue(now));
  publisher.Tick(now);
  assert(!publisher.BlockingStepDue(now));
  publisher.Tick(now);
  assert(publisher.streaming());
  output.clear();
  Gateway::Frame frame{};
  frame.size = 38;
  frame.receipt_ns = now;
  frame.bytes[0] = 0xd3;
  frame.bytes[2] = 32;
  frame.bytes[3] = 1005 >> 4;
  frame.bytes[4] = (1005 & 15) << 4;
  std::uint32_t crc{};
  assert(ug_rtcm_crc24q(frame.bytes.data(), frame.size - 3, &crc));
  for (unsigned i = 0; i < 3; ++i)
    frame.bytes[frame.size - 3 + i] = crc >> (16 - 8 * i);
  NtripPublisher::PendingFrame pending;
  chunk = 7;
  assert(publisher.BeginFrame(pending, frame, now) && pending.active && pending.offset == 7);
  assert(!publisher.BeginFrame(pending, frame, now));
  assert(publisher.metrics().frames == 0);
  wait_write = true;
  const auto before = writes;
  publisher.ProgressFrame(pending, now + 1);
  assert(writes == before + 1 && pending.active && pending.offset == 7);
  assert(publisher.metrics().frames == 0);
  wait_write = false;
  while (pending.active)
    publisher.ProgressFrame(pending, ++now);
  assert(publisher.metrics().frames == 1 && publisher.metrics().bytes == frame.size);
  assert(output ==
         std::vector<std::uint8_t>(frame.bytes.begin(), frame.bytes.begin() + frame.size));
  assert(!publisher.BeginFrame(pending, frame, now));  // Filter advances only on full send.
  now += 10000000001LL;
  frame.receipt_ns = now;
  wait_write = true;
  assert(publisher.BeginFrame(pending, frame, now) && pending.offset == 0);
  publisher.ProgressFrame(pending, now + 1000000000LL);
  assert(!pending.active && !publisher.streaming() && publisher.metrics().frames == 1);
  // Failed/expired transmission did not consume another ten-second filter slot.
  wait_write = false;
  chunk = 4096;
  now += 5000000000LL;
  assert(publisher.Configure(config, true, {}, "1005(10)", true));
  publisher.Tick(now);
  publisher.Tick(now);
  publisher.Tick(now);
  frame.receipt_ns = now;
  assert(publisher.BeginFrame(pending, frame, now) && !pending.active);
  assert(publisher.metrics().frames == 2);
  // Partial frame must not be concatenated across a failed/replaced connection.
  now += 11000000000LL;
  frame.receipt_ns = now;
  chunk = 1;
  assert(publisher.BeginFrame(pending, frame, now));
  break_write = true;
  publisher.ProgressFrame(pending, now + 1);
  assert(!pending.active && !publisher.streaming() && publisher.metrics().frames == 2);
}
