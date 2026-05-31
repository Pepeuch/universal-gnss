#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/ublox_session.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::HasCapability;
using universal_gnss::HasValueAvailable;
using universal_gnss_driver::UbloxSession;

struct TestContext
{
  int failures{0};

  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

bool NearlyEqual(const double lhs, const double rhs, const double tolerance = 1e-6)
{
  const double delta = lhs - rhs;
  return delta <= tolerance && delta >= -tolerance;
}

std::vector<std::uint8_t> BuildNmeaSentence(const std::string& payload,
                                            const bool valid_checksum = true)
{
  std::vector<std::uint8_t> bytes;
  bytes.push_back(static_cast<std::uint8_t>('$'));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.push_back(static_cast<std::uint8_t>('*'));

  std::uint8_t checksum = universal_gnss_protocols::ComputeNmeaChecksum(payload);
  if (!valid_checksum)
  {
    checksum ^= 0x01u;
  }

  constexpr char kHexDigits[] = "0123456789ABCDEF";
  bytes.push_back(static_cast<std::uint8_t>(kHexDigits[(checksum >> 4u) & 0x0Fu]));
  bytes.push_back(static_cast<std::uint8_t>(kHexDigits[checksum & 0x0Fu]));
  bytes.push_back(static_cast<std::uint8_t>('\r'));
  bytes.push_back(static_cast<std::uint8_t>('\n'));
  return bytes;
}

void WriteLeU2(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::uint16_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void WriteLeU4(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::uint32_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
  payload[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
  payload[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

void WriteLeI4(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::int32_t value)
{
  WriteLeU4(payload, offset, static_cast<std::uint32_t>(value));
}

void WriteLeI2(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::int16_t value)
{
  WriteLeU2(payload, offset, static_cast<std::uint16_t>(value));
}

std::vector<std::uint8_t> BuildUbxFrame(const std::uint8_t class_id,
                                        const std::uint8_t message_id,
                                        const std::vector<std::uint8_t>& payload,
                                        const bool valid_checksum = true)
{
  std::vector<std::uint8_t> bytes = {
      0xB5u,
      0x62u,
      class_id,
      message_id,
      static_cast<std::uint8_t>(payload.size() & 0xFFu),
      static_cast<std::uint8_t>((payload.size() >> 8u) & 0xFFu),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const auto checksum =
      universal_gnss_protocols::ComputeUbxChecksum(bytes.data() + 2u, bytes.size() - 2u);
  bytes.push_back(valid_checksum ? checksum.ck_a : static_cast<std::uint8_t>(checksum.ck_a ^ 0x01u));
  bytes.push_back(checksum.ck_b);
  return bytes;
}

std::vector<std::uint8_t> BuildRtcmFrame(const std::uint16_t message_type,
                                         const bool valid_crc = true)
{
  const std::vector<std::uint8_t> payload = {
      static_cast<std::uint8_t>((message_type >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((message_type & 0x0Fu) << 4u),
  };

  std::vector<std::uint8_t> bytes = {
      0xD3u,
      0x00u,
      static_cast<std::uint8_t>(payload.size()),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  if (!valid_crc)
  {
    crc ^= 0x01u;
  }

  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

void Append(std::vector<std::uint8_t>& destination, const std::vector<std::uint8_t>& source)
{
  destination.insert(destination.end(), source.begin(), source.end());
}

std::vector<std::uint8_t> MakeNavPvtPayload()
{
  std::vector<std::uint8_t> payload(92u, 0u);
  WriteLeU4(payload, 0u, 345000u);
  WriteLeU2(payload, 4u, 2025u);
  payload[6u] = 5u;
  payload[7u] = 28u;
  payload[8u] = 12u;
  payload[9u] = 34u;
  payload[10u] = 56u;
  payload[11u] = 0x07u;
  WriteLeI4(payload, 16u, 123456789);

  payload[20u] = 3u;
  payload[21u] = static_cast<std::uint8_t>(0x01u | (1u << 5));
  payload[23u] = 18u;

  WriteLeI4(payload, 24u, 231234567);
  WriteLeI4(payload, 28u, 485678901);
  WriteLeI4(payload, 32u, 123450);
  WriteLeI4(payload, 36u, 120000);
  WriteLeU4(payload, 40u, 250u);
  WriteLeU4(payload, 44u, 500u);
  WriteLeI4(payload, 84u, 12345678);
  return payload;
}

std::vector<std::uint8_t> MakeNavSatPayload()
{
  std::vector<std::uint8_t> payload(8u + (3u * 12u), 0u);
  WriteLeU4(payload, 0u, 456000u);
  payload[4u] = 0x01u;
  payload[5u] = 3u;

  payload[8u] = 0u;
  payload[9u] = 4u;
  payload[10u] = 45u;
  payload[11u] = 30u;
  WriteLeI2(payload, 12u, 120);
  WriteLeI2(payload, 14u, 0);
  WriteLeU4(payload, 16u, 0x0000001Cu);

  payload[20u] = 2u;
  payload[21u] = 12u;
  payload[22u] = 38u;
  payload[23u] = 15u;
  WriteLeI2(payload, 24u, 220);
  WriteLeI2(payload, 26u, 0);
  WriteLeU4(payload, 28u, 0x00000004u);

  payload[32u] = 0u;
  payload[33u] = 18u;
  payload[34u] = 0u;
  payload[35u] = 5u;
  WriteLeI2(payload, 36u, 300);
  WriteLeI2(payload, 38u, 0);
  WriteLeU4(payload, 40u, 0x0000002Cu);
  return payload;
}

std::vector<std::uint8_t> MakeNavDopPayload()
{
  std::vector<std::uint8_t> payload(18u, 0u);
  WriteLeU4(payload, 0u, 654321u);
  WriteLeU2(payload, 4u, 145u);
  WriteLeU2(payload, 6u, 123u);
  WriteLeU2(payload, 8u, 99u);
  WriteLeU2(payload, 10u, 87u);
  WriteLeU2(payload, 12u, 65u);
  WriteLeU2(payload, 14u, 111u);
  WriteLeU2(payload, 16u, 109u);
  return payload;
}

std::vector<std::uint8_t> MakeNavStatusPayload()
{
  std::vector<std::uint8_t> payload(16u, 0u);
  WriteLeU4(payload, 0u, 456789u);
  payload[4u] = 3u;
  payload[5u] = static_cast<std::uint8_t>((1u << 0) | (1u << 1));
  payload[6u] = static_cast<std::uint8_t>(1u << 1);
  payload[7u] = static_cast<std::uint8_t>(2u << 6);
  WriteLeU4(payload, 8u, 1500u);
  WriteLeU4(payload, 12u, 42000u);
  return payload;
}

std::vector<std::uint8_t> MakeMonRfPayload(const std::uint8_t first_jamming_state,
                                           const std::uint8_t second_jamming_state = 1u)
{
  std::vector<std::uint8_t> payload(4u + (2u * 24u), 0u);
  payload[0u] = 0x00u;
  payload[1u] = 2u;

  payload[4u] = 0u;
  payload[5u] = first_jamming_state;
  payload[6u] = 0x02u;
  payload[7u] = 0x01u;
  WriteLeU4(payload, 8u, 0x12345678u);
  WriteLeU2(payload, 16u, 150u);
  WriteLeU2(payload, 18u, 4096u);
  payload[20u] = 20u;

  payload[28u] = 1u;
  payload[29u] = second_jamming_state;
  payload[30u] = 0x02u;
  payload[31u] = 0x01u;
  WriteLeU4(payload, 32u, 0x87654321u);
  WriteLeU2(payload, 40u, 250u);
  WriteLeU2(payload, 42u, 5000u);
  payload[44u] = 0u;
  return payload;
}

void TestNavPvtRuntimeUpdates(TestContext& ctx)
{
  UbloxSession session;
  session.FeedBytes(BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload()), 1111);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.ubx_frames_seen == 1u && metrics.frames_parsed == 1u &&
                 metrics.runtime_updates == 1u,
             "NAV-PVT should count as one parsed runtime-updating UBX frame");
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(1111) &&
                 state.fix_valid && state.fix_type == GnssFixType::kFix,
             "NAV-PVT should update fix state");
  ctx.Expect(state.latitude_deg == std::optional<double>(48.5678901) &&
                 state.longitude_deg == std::optional<double>(23.1234567) &&
                 state.altitude_m == std::optional<double>(120.0),
             "NAV-PVT should update coordinates and altitude");
  ctx.Expect(HasCapability(state, GnssCapability::kHeading) &&
                 HasValueAvailable(state, GnssCapability::kHeading) &&
                 state.heading_deg.has_value() &&
                 NearlyEqual(*state.heading_deg, 123.45678),
             "NAV-PVT should update heading when valid");
}

void TestNavSatCn0AndSatelliteUpdates(TestContext& ctx)
{
  UbloxSession session;
  session.FeedBytes(BuildUbxFrame(0x01u, 0x35u, MakeNavSatPayload()), 2222);

  const auto& state = session.current_state();
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(2222) &&
                 state.satellites_visible == std::optional<std::uint16_t>(3u) &&
                 state.satellites_used == std::optional<std::uint16_t>(2u),
             "NAV-SAT should update visible and used satellite counts");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kMeanCn0) &&
                 HasValueAvailable(state, GnssCapability::kMaxCn0) &&
                 state.mean_cn0_db_hz == std::optional<float>(41.5f) &&
                 state.max_cn0_db_hz == std::optional<float>(45.0f),
             "NAV-SAT should update CN0 mean and max");
}

void TestNavStatusRtkUpdates(TestContext& ctx)
{
  UbloxSession session;
  session.FeedBytes(BuildUbxFrame(0x01u, 0x03u, MakeNavStatusPayload()), 3333);

  const auto& state = session.current_state();
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(3333) &&
                 state.fix_valid && state.fix_type == GnssFixType::kFix &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed),
             "NAV-STATUS should update fix state and RTK mode");
}

void TestMonRfInterferenceAndJammingUpdates(TestContext& ctx)
{
  UbloxSession session;
  session.FeedBytes(BuildUbxFrame(0x0Au, 0x38u, MakeMonRfPayload(3u)), 4444);

  const auto& state = session.current_state();
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(4444) &&
                 state.interference_detected == std::optional<bool>(true) &&
                 state.jamming_detected == std::optional<bool>(true),
             "MON-RF should update interference and jamming state");
}

void TestNavDopUpdatesHdopAndVdop(TestContext& ctx)
{
  UbloxSession session;
  session.FeedBytes(BuildUbxFrame(0x01u, 0x04u, MakeNavDopPayload()), 4545);

  const auto& state = session.current_state();
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(4545) &&
                 HasCapability(state, GnssCapability::kHdop) &&
                 HasCapability(state, GnssCapability::kVdop) &&
                 HasValueAvailable(state, GnssCapability::kHdop) &&
                 HasValueAvailable(state, GnssCapability::kVdop) &&
                 state.hdop.has_value() && NearlyEqual(*state.hdop, 0.65) &&
                 state.vdop.has_value() && NearlyEqual(*state.vdop, 0.87),
             "NAV-DOP should update HDOP and VDOP conservatively");
  ctx.Expect(!state.fix_valid &&
                 state.fix_type == GnssFixType::kUnknown &&
                 !state.latitude_deg.has_value() &&
                 !state.satellites_visible.has_value(),
             "NAV-DOP should not invent fix, position, or satellite state");
}

void TestNmeaGstAccuracyUpdates(TestContext& ctx)
{
  UbloxSession session;
  session.FeedBytes(BuildNmeaSentence(
                        "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),
                    5000);
  session.FeedBytes(BuildNmeaSentence(
                        "GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,0.6,1.1"),
                    5001);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.nmea_sentences_seen == 2u &&
                 metrics.frames_parsed == 2u &&
                 metrics.runtime_updates == 2u,
             "GGA plus GST should count as two parsed runtime updates");
  ctx.Expect(state.fix_valid && state.fix_type == GnssFixType::kFix,
             "GST should not disturb the existing NMEA fix state");
  ctx.Expect(state.latitude_deg.has_value() && NearlyEqual(*state.latitude_deg, 48.1173) &&
                 state.longitude_deg.has_value() &&
                 NearlyEqual(*state.longitude_deg, 11.5166667) &&
                 state.altitude_m.has_value() && NearlyEqual(*state.altitude_m, 545.4),
             "GST should not overwrite position from GGA");
  ctx.Expect(HasCapability(state, GnssCapability::kHorizontalAccuracy) &&
                 HasCapability(state, GnssCapability::kVerticalAccuracy) &&
                 HasValueAvailable(state, GnssCapability::kHorizontalAccuracy) &&
                 HasValueAvailable(state, GnssCapability::kVerticalAccuracy) &&
                 state.horizontal_accuracy_m == std::optional<float>(0.6f) &&
                 state.vertical_accuracy_m == std::optional<float>(1.1f),
             "GST should enrich the session state with conservative accuracy fields");
  ctx.Expect(!HasCapability(state, GnssCapability::kRtkMode),
             "GST should not invent RTK capability in the session state");
}

void TestMixedStreamRouting(TestContext& ctx)
{
  std::vector<std::uint8_t> stream = {0x00u, 0x7Fu};
  Append(stream, BuildNmeaSentence(
                     "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
  Append(stream, BuildNmeaSentence(
                     "GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5"));
  Append(stream, BuildNmeaSentence(
                     "GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"));
  Append(stream, BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload()));
  Append(stream, BuildRtcmFrame(1005u));

  UbloxSession session;
  session.FeedBytes(stream);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.nmea_sentences_seen == 3u &&
                 metrics.ubx_frames_seen == 1u &&
                 metrics.rtcm_frames_seen == 1u,
             "mixed stream should count NMEA, UBX, and RTCM frames");
  ctx.Expect(metrics.frames_parsed == 5u &&
                 metrics.runtime_updates == 4u &&
                 metrics.rtcm_message_type_counts.at(1005u) == 1u,
             "mixed stream should parse supported messages and retain RTCM counts");
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kFix &&
                 state.latitude_deg == std::optional<double>(48.5678901) &&
                 state.satellites_visible == std::optional<std::uint16_t>(8u),
             "mixed stream should merge NMEA and UBX runtime fields");
}

void TestUnknownAndMalformedFrameCounting(TestContext& ctx)
{
  UbloxSession session;
  session.FeedBytes(BuildUbxFrame(0x06u, 0x01u, std::vector<std::uint8_t>(8u, 0u)));
  session.FeedBytes(BuildUbxFrame(0x01u, 0x03u, MakeNavStatusPayload(), false));
  session.FeedBytes(BuildNmeaSentence(
                        "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,",
                        false));

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.unknown_frames == 1u,
             "unsupported valid UBX frames should increment unknown-frame count");
  ctx.Expect(metrics.malformed_frames == 2u &&
                 metrics.frames_rejected == 0u,
             "invalid checksum UBX and NMEA frames should count as malformed, not rejected");
}

void TestPartialChunksAcrossFeedCalls(TestContext& ctx)
{
  const auto nav_pvt = BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload());
  UbloxSession session;
  session.FeedBytes(nav_pvt.data(), 12u, 7000);
  session.FeedBytes(nav_pvt.data() + 12u, nav_pvt.size() - 12u, 7001);

  ctx.Expect(session.metrics().ubx_frames_seen == 1u &&
                 session.metrics().frames_parsed == 1u &&
                 session.current_state().timestamp_ns == std::optional<std::int64_t>(7000),
             "split NAV-PVT input should parse after the final chunk and preserve the first-byte timestamp");
}

void TestFinalizeAndReset(TestContext& ctx)
{
  UbloxSession session;
  const auto truncated = BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload());
  session.FeedBytes(truncated.data(), 10u);
  session.Finalize();

  ctx.Expect(session.metrics().malformed_frames == 1u,
             "finalizing a truncated UBX tail should count a malformed frame");

  session.FeedBytes(BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload()), 8000);
  session.Reset();
  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.bytes_seen == 0u && metrics.ubx_frames_seen == 0u &&
                 metrics.frames_parsed == 0u && metrics.runtime_updates == 0u &&
                 metrics.malformed_frames == 0u,
             "reset should clear UbloxSession metrics");
  ctx.Expect(state.fix_type == GnssFixType::kUnknown && !state.fix_valid &&
                 !state.latitude_deg.has_value(),
             "reset should clear the aggregated runtime state");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestNavPvtRuntimeUpdates(ctx);
  TestNavSatCn0AndSatelliteUpdates(ctx);
  TestNavStatusRtkUpdates(ctx);
  TestMonRfInterferenceAndJammingUpdates(ctx);
  TestNavDopUpdatesHdopAndVdop(ctx);
  TestNmeaGstAccuracyUpdates(ctx);
  TestMixedStreamRouting(ctx);
  TestUnknownAndMalformedFrameCounting(ctx);
  TestPartialChunksAcrossFeedCalls(ctx);
  TestFinalizeAndReset(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver Ublox session tests passed\n";
  return EXIT_SUCCESS;
}
