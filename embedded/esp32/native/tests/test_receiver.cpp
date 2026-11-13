// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_receiver_runtime.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_transport/memory_stream.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
using namespace universal_gnss_driver;
static ReceiverCommand Command()
{
  ReceiverCommand c;
  c.kind = ReceiverCommandKind::kQuery;
  c.target.vendor = ReceiverVendor::kUnicore;
  c.expected_response = ReceiverResponseKind::kTextPayload;
  c.retry_policy.timeout_ms = 10;
  SetTextPayload(c, "VERSION\r\n");
  return c;
}
static void Feed(ug::embedded::Receiver& r, const std::string& s, std::int64_t now)
{
  assert(r.Feed(reinterpret_cast<const std::uint8_t*>(s.data()), s.size(), now));
}
int main()
{
  universal_gnss_transport::MemoryByteSink sink;
  ReceiverSessionConfig config;
  config.kind = ReceiverSessionKind::kNmea;
  ug::embedded::Receiver r(sink, config);
  const std::string payload =
      "GNGGA,123519,4807.038,N,01131.000,E,4,12,0.8,545.4,M,46.9,M,1.0,0000";
  char checksum[8];
  std::snprintf(checksum,
                sizeof(checksum),
                "*%02X\r\n",
                universal_gnss_protocols::ComputeNmeaChecksum(payload));
  Feed(r, "$" + payload + checksum, 1);
  assert(r.state().fix_valid && r.state().latitude_deg && r.state().longitude_deg);
  assert(std::abs(*r.state().latitude_deg - 48.1173) < 1e-10);
  const auto observations = r.metrics().runtime_observations;
  r.Step(2);  // Cached publication/tick is not a physical observation.
  assert(r.metrics().runtime_observations == observations);
  r.InvalidateObservations();
  assert(!r.state().latitude_deg && !r.state().fix_valid && r.incarnation() == 2);
  auto command = Command();
  command.safety_level = ReceiverCommandSafetyLevel::kPersistent;
  assert(!r.Start({command}, 3));
  command = Command();
  Feed(r, "<O", 3);
  assert(r.Start({command, command}, 4));
  Feed(r, "K\r\n", 5);  // Prefix captured before dispatch cannot ACK command 1.
  assert(r.application().metrics().commands_completed == 0);
  Feed(r, "<OK\r\n<OK\r\n", 6);  // One capture cannot acknowledge two commands.
  assert(r.application().metrics().commands_completed == 1);
  r.Step(7);
  assert(r.application().metrics().commands_started == 2);
  assert(r.application().metrics().commands_completed == 1);
  Feed(r, "<OK\r\n", 8);
  assert(r.application().state() == ReceiverConfigApplicationState::kCompleted);
  assert(r.Start({command}, 9));
  r.Step(10000009);
  assert(r.indeterminate());
  r.InvalidateObservations();  // Neither reconnect nor parser reset clears quarantine.
  assert(r.indeterminate() && !r.Start({command}, 10000010));
  assert(!r.Feed(nullptr, 1, 10000011));
  assert(!r.Feed(reinterpret_cast<const std::uint8_t*>(payload.data()), 513, 10000011));
  assert(!r.Feed(nullptr, 0, -1));
}
