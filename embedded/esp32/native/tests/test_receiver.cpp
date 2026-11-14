// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_receiver_runtime.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
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
static std::vector<std::uint8_t> Survey(std::uint32_t tow)
{
  std::vector<std::uint8_t> bytes{0xb5, 0x62, 1, 0x3b, 40, 0};
  bytes.resize(46);
  for (unsigned i = 0; i < 4; ++i)
    bytes[10 + i] = static_cast<std::uint8_t>(tow >> (8 * i));
  bytes[42] = 1;  // Valid, inactive: raw receiver report, not an operation ACK.
  const auto crc = universal_gnss_protocols::ComputeUbxChecksum(bytes.data() + 2, bytes.size() - 2);
  bytes.push_back(crc.ck_a);
  bytes.push_back(crc.ck_b);
  return bytes;
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
  r.ConfigureTelemetry(config);
  assert(r.indeterminate() && !r.Start({command}, 10000010));
  assert(!r.state().fix_valid && !r.state().latitude_deg);
  assert(!r.Feed(nullptr, 1, 10000011));
  assert(!r.Feed(reinterpret_cast<const std::uint8_t*>(payload.data()), 513, 10000011));
  assert(!r.Feed(nullptr, 0, -1));
  // Input loss during a pending operation immediately invalidates/quarantines,
  // even before its normal response timeout; a later ACK cannot release it.
  ug::embedded::Receiver surveyed(sink, config);
  const auto survey = Survey(1000);
  assert(surveyed.Feed(survey.data(), 20, 1));
  assert(!surveyed.survey());
  assert(surveyed.Feed(survey.data() + 20, survey.size() - 20, 2));
  assert(surveyed.survey() && surveyed.survey()->valid && !surveyed.survey()->active);
  assert(surveyed.survey()->timestamp_ns == 1);
  assert(surveyed.Feed(survey.data(), survey.size(), 9000000000LL));
  assert(surveyed.survey()->timestamp_ns == 1);  // Duplicate epoch never renews freshness.
  const auto next = Survey(2000);
  assert(surveyed.Feed(next.data(), next.size(), 9000000001LL));
  assert(surveyed.survey()->timestamp_ns == 9000000001LL);
  assert(surveyed.Start({command}, 9000000002LL));
  assert(!surveyed.survey());  // Earlier status cannot qualify the next operation.
  surveyed.InvalidateObservations();
  assert(!surveyed.survey() && surveyed.indeterminate());
  const std::array<std::uint8_t, 8> poll{0xb5, 0x62, 1, 0x3b, 0, 0, 0x3c, 0xb5};
  universal_gnss_protocols::UbxFrameFramer poll_framer;
  universal_gnss_protocols::ParserResult<universal_gnss_protocols::UbxFrame> polled;
  for (const auto byte : poll)
    polled = poll_framer.PushByte(byte);
  assert(polled.record && polled.record->payload.empty() && polled.record->message_id == 0x3b);
  ug::embedded::Receiver interrupted(sink, config);
  assert(interrupted.Start({command}, 1));
  Feed(interrupted, "<O", 2);
  interrupted.InvalidateObservations();
  assert(interrupted.indeterminate());
  Feed(interrupted, "K\r\n<OK\r\n", 3);
  assert(interrupted.indeterminate() && !interrupted.Start({command}, 4));
}
