#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/unicore_session.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::HasCapability;
using universal_gnss::HasValueAvailable;
using universal_gnss_driver::UnicoreSession;
using universal_gnss_driver::UnicoreSessionConfig;

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
  return std::fabs(lhs - rhs) <= tolerance;
}

constexpr const char* kBestNavLine =
    "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
    "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
    "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
    "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n";

constexpr const char* kPvtslnLine =
    "#PVTSLNA,97,GPS,FINE,2190,364536000,0,0,18,13;"
    "NARROW_INT,60.5060,40.07898130522,116.23663134427,0.2000,0.1500,0.1800,0.9000,"
    "SINGLE,60.5060,40.07898130522,116.23663134427,4.3353,46,28,46,28,0.0009,-0.0031,-0.0032,"
    "SOL_COMPUTED,1.5000,182.2500,0.1000,28,25,12,8,2.1753,1.3480,0.6840,1.8392,1.7072,5.0,"
    "28,25,26*1e33c8cb\r\n";

constexpr const char* kRtkStatusLine =
    "#RTKSTATUSA,97,GPS,FINE,2190,365354000,0,0,18,1;"
    "0,0,0,0,0,0,0,0,0,0,0,NARROW_INT,5,0,1,12,0*f06a8a06\r\n";

constexpr const char* kSatsInfoLine =
    "#SATSINFOA,96,GPS,FINE,2215,367199000,0,0,18,16;"
    "3,2,0,0,0,63,"
    "2,302,51,0,45,0,2,0,42,9,2,"
    "4,48,17,0,37,0,3,0,43,14,3,0,39,9,3,"
    "5,225,14,1,50,0,1*abcdef12\r\n";

constexpr const char* kBestSatLine =
    "#BESTSATA,79,GPS,FINE,2203,226245800,0,0,18,22;"
    "4,GPS,2,GOOD,00000013,GLONASS,2-4,GOOD,00000010,GALILEO,5,GOOD,00000001,BEIDOU,20,GOOD,00000000*12345678\r\n";

constexpr const char* kJamStatusLine =
    "#JAMSTATUSA,97,GPS,FINE,2190,365412000,0,0,18,14;SINGLE,120,2,0,0*e31418ea\r\n";

constexpr const char* kRtcmStatusLine =
    "#RTCMSTATUSA,76,GPS,FINE,2219,392572000,0,0,18,187;"
    "1124,21186,0,21,0,6,11,0,0,21*601a7581\r\n";

constexpr const char* kHwStatusLine =
    "#HWSTATUSA,97,GPS,FINE,2221,111183000,0,0,18,15;66807,0.920,1.020,0.908,1,0.693,0.0,0x00,0,0x0377,0,0*9d7ce51d\r\n";

void AppendLittleEndian16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void AppendLittleEndian32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void WriteLittleEndian32(std::vector<std::uint8_t>& bytes,
                         const std::size_t offset,
                         const std::uint32_t value)
{
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void WriteLittleEndianFloat32(std::vector<std::uint8_t>& bytes,
                              const std::size_t offset,
                              const float value)
{
  std::uint32_t raw = 0u;
  std::memcpy(&raw, &value, sizeof(raw));
  WriteLittleEndian32(bytes, offset, raw);
}

void WriteLittleEndianFloat64(std::vector<std::uint8_t>& bytes,
                              const std::size_t offset,
                              const double value)
{
  std::uint64_t raw = 0u;
  std::memcpy(&raw, &value, sizeof(raw));
  bytes[offset] = static_cast<std::uint8_t>(raw & 0xFFu);
  bytes[offset + 1u] = static_cast<std::uint8_t>((raw >> 8) & 0xFFu);
  bytes[offset + 2u] = static_cast<std::uint8_t>((raw >> 16) & 0xFFu);
  bytes[offset + 3u] = static_cast<std::uint8_t>((raw >> 24) & 0xFFu);
  bytes[offset + 4u] = static_cast<std::uint8_t>((raw >> 32) & 0xFFu);
  bytes[offset + 5u] = static_cast<std::uint8_t>((raw >> 40) & 0xFFu);
  bytes[offset + 6u] = static_cast<std::uint8_t>((raw >> 48) & 0xFFu);
  bytes[offset + 7u] = static_cast<std::uint8_t>((raw >> 56) & 0xFFu);
}

std::vector<std::uint8_t> BuildUnicoreBinaryFrame(const std::uint16_t message_id,
                                                  const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> frame = {0xAAu, 0x44u, 0xB5u, 97u};
  AppendLittleEndian16(frame, message_id);
  AppendLittleEndian16(frame, static_cast<std::uint16_t>(payload.size()));
  frame.push_back(0u);
  frame.push_back(1u);
  AppendLittleEndian16(frame, 2190u);
  AppendLittleEndian32(frame, 364536000u);
  AppendLittleEndian32(frame, 18u);
  frame.push_back(0u);
  frame.push_back(13u);
  AppendLittleEndian16(frame, 0u);
  frame.insert(frame.end(), payload.begin(), payload.end());

  const std::uint32_t crc =
      universal_gnss_protocols::ComputeUnicoreBinaryCrc32(frame.data(), frame.size());
  AppendLittleEndian32(frame, crc);
  return frame;
}

std::vector<std::uint8_t> MakeBestNavBPayload()
{
  std::vector<std::uint8_t> payload(120u, 0u);
  WriteLittleEndian32(payload, 0u, 0u);
  WriteLittleEndian32(payload, 4u, 34u);
  WriteLittleEndianFloat64(payload, 8u, 40.0789588272);
  WriteLittleEndianFloat64(payload, 16u, 116.2365102982);
  WriteLittleEndianFloat64(payload, 24u, 65.8312);
  WriteLittleEndianFloat32(payload, 40u, 1.2221f);
  WriteLittleEndianFloat32(payload, 44u, 1.1053f);
  WriteLittleEndianFloat32(payload, 48u, 2.1970f);
  WriteLittleEndianFloat32(payload, 56u, 0.4f);
  payload[64u] = 50u;
  payload[65u] = 28u;
  return payload;
}

std::vector<std::uint8_t> MakePvtslnBPayload()
{
  std::vector<std::uint8_t> payload(224u, 0u);
  WriteLittleEndian32(payload, 0u, 50u);
  WriteLittleEndianFloat32(payload, 4u, 60.5060f);
  WriteLittleEndianFloat64(payload, 8u, 40.07898130522);
  WriteLittleEndianFloat64(payload, 16u, 116.23663134427);
  WriteLittleEndianFloat32(payload, 24u, 0.2000f);
  WriteLittleEndianFloat32(payload, 28u, 0.1500f);
  WriteLittleEndianFloat32(payload, 32u, 0.1800f);
  WriteLittleEndianFloat32(payload, 36u, 0.9000f);
  payload[68u] = 46u;
  payload[69u] = 28u;
  WriteLittleEndian32(payload, 96u, 0u);
  WriteLittleEndianFloat32(payload, 104u, 182.2500f);
  WriteLittleEndianFloat32(payload, 124u, 0.6840f);
  return payload;
}

void TestBestNavUpdatesRuntimeState(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kBestNavLine, 2222);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.bytes_seen == std::string(kBestNavLine).size(),
             "BESTNAVA feed should count input bytes");
  ctx.Expect(metrics.lines_seen == 1u && metrics.ascii_records_seen == 1u &&
                 metrics.records_parsed == 1u && metrics.runtime_updates == 1u,
             "BESTNAVA feed should count one parsed runtime update");
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(2222) &&
                 state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFloat &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFloat),
             "BESTNAVA should update fix and RTK state");
  ctx.Expect(state.latitude_deg == std::optional<double>(40.0789588272) &&
                 state.longitude_deg == std::optional<double>(116.2365102982) &&
                 state.altitude_m == std::optional<double>(65.8312),
             "BESTNAVA should update coordinates and altitude");
}

void TestPvtslnUpdatesHeading(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kPvtslnLine, 1111);

  const auto& state = session.current_state();
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFixed &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed),
             "PVTSLNA should expose RTK fixed state");
  ctx.Expect(HasCapability(state, GnssCapability::kHeading) &&
                 HasValueAvailable(state, GnssCapability::kHeading) &&
                 state.heading_deg == std::optional<double>(182.25),
             "PVTSLNA should update heading when the message reports a computed heading");
}

void TestRtkStatusUpdatesDualAntenna(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kRtkStatusLine, 3333);

  const auto& state = session.current_state();
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFixed &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed),
             "RTKSTATUSA should update RTK fixed state");
  ctx.Expect(HasCapability(state, GnssCapability::kDualAntennaHeading) &&
                 HasValueAvailable(state, GnssCapability::kDualAntennaHeading) &&
                 state.dual_antenna_heading == std::optional<bool>(true),
             "RTKSTATUSA should update dual-antenna heading state");
}

void TestSatsInfoUpdatesTrackedAndCn0(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kSatsInfoLine, 5555);

  const auto& state = session.current_state();
  ctx.Expect(HasCapability(state, GnssCapability::kSatellitesTracked) &&
                 HasValueAvailable(state, GnssCapability::kSatellitesTracked) &&
                 state.satellites_tracked == std::optional<std::uint16_t>(3u),
             "SATSINFOA should update tracked-satellite count");
  ctx.Expect(HasCapability(state, GnssCapability::kMeanCn0) &&
                 HasCapability(state, GnssCapability::kMaxCn0) &&
                 HasValueAvailable(state, GnssCapability::kMeanCn0) &&
                 HasValueAvailable(state, GnssCapability::kMaxCn0) &&
                 state.mean_cn0_db_hz == std::optional<float>(46.0f) &&
                 state.max_cn0_db_hz == std::optional<float>(50.0f),
             "SATSINFOA should update CN0 summaries");
}

void TestBestSatUpdatesTrackedAndUsedOnly(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kBestSatLine, 5656);

  const auto& state = session.current_state();
  ctx.Expect(HasCapability(state, GnssCapability::kSatellitesTracked) &&
                 HasCapability(state, GnssCapability::kSatellitesUsed) &&
                 HasValueAvailable(state, GnssCapability::kSatellitesTracked) &&
                 HasValueAvailable(state, GnssCapability::kSatellitesUsed) &&
                 state.satellites_tracked == std::optional<std::uint16_t>(4u) &&
                 state.satellites_used == std::optional<std::uint16_t>(2u),
             "BESTSATA should update conservative tracked and used satellite counts");
  ctx.Expect(!HasCapability(state, GnssCapability::kMeanCn0) &&
                 !HasCapability(state, GnssCapability::kMaxCn0) &&
                 !HasCapability(state, GnssCapability::kSatellitesVisible) &&
                 state.fix_type == GnssFixType::kUnknown,
             "BESTSATA should not invent CN0, visibility, or fix state");
}

void TestJammingStatusUpdatesRuntimeState(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kJamStatusLine, 6666);

  const auto& state = session.current_state();
  ctx.Expect(HasCapability(state, GnssCapability::kInterferenceState) &&
                 HasCapability(state, GnssCapability::kJammingState) &&
                 HasValueAvailable(state, GnssCapability::kInterferenceState) &&
                 HasValueAvailable(state, GnssCapability::kJammingState) &&
                 state.interference_detected == std::optional<bool>(true) &&
                 state.jamming_detected == std::optional<bool>(true),
             "JAMSTATUSA should update the portable interference and jamming state");
  ctx.Expect(!state.latitude_deg.has_value() && state.fix_type == GnssFixType::kUnknown,
             "JAMSTATUSA should not invent position or fix state");
}

void TestRtcmStatusParsesWithoutRuntimeUpdate(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kRtcmStatusLine, 7777);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.ascii_records_seen == 1u &&
                 metrics.records_parsed == 1u &&
                 metrics.runtime_updates == 0u,
             "RTCMSTATUSA should be parsed by the session without becoming a runtime update");
  ctx.Expect(metrics.receiver_rtcm_status_messages_seen == 1u &&
                 metrics.receiver_rtcm_status_message_count == 21186u &&
                 metrics.receiver_last_rtcm_message_type == std::optional<std::uint32_t>(1124u) &&
                 metrics.receiver_last_rtcm_base_station_id == std::optional<std::uint32_t>(0u) &&
                 metrics.receiver_last_rtcm_satellites_in_message ==
                     std::optional<std::uint32_t>(21u),
             "RTCMSTATUSA should expose receiver-side RTCM status metrics");
  ctx.Expect(session.current_state().fix_type == GnssFixType::kUnknown &&
                 !session.current_state().timestamp_ns.has_value(),
             "RTCMSTATUSA should stay out of the aggregated runtime state");
}

void TestUnknownAndMalformedRecords(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString("#FOOBARA,97,GPS,FINE,1,2,0,0,0,0;payload\r\n");
  session.FeedString(
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,SINGLE,not_a_latitude,116.2365102982,65.8312*abcd1234\r\n");

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.lines_seen == 2u && metrics.ascii_records_seen == 2u,
             "unknown and malformed lines should still count as seen records");
  ctx.Expect(metrics.unknown_records == 1u && metrics.records_rejected == 1u &&
                 metrics.records_parsed == 0u && metrics.runtime_updates == 0u,
             "unknown and malformed records should be counted separately");
  ctx.Expect(session.current_state().fix_type == GnssFixType::kUnknown,
             "unknown and malformed records should not invent runtime state");
}

void TestPartialChunksAcrossFeeds(TestContext& ctx)
{
  UnicoreSession session;
  const std::string line = kBestNavLine;
  session.FeedString(std::string_view(line.data(), 32u), 7000);
  session.FeedString(std::string_view(line.data() + 32u, line.size() - 32u), 7001);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.lines_seen == 1u && metrics.records_parsed == 1u &&
                 metrics.runtime_updates == 1u,
             "split BESTNAVA input should parse after the final chunk arrives");
  ctx.Expect(session.current_state().timestamp_ns == std::optional<std::int64_t>(7000),
             "split input should preserve the first-byte timestamp of the framed record");
}

void TestHardwareAndAgcRecordsCountAsParsedWithoutRuntimeUpdate(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kHwStatusLine, 9000);
  session.FeedString(
      "#AGCA,65,GPS,FINE,2190,375570000,0,0,18,37;44,46,63,-1,-1,41,1,0,-1,-1*634f1e4b\r\n",
      9001);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.records_parsed == 2u && metrics.runtime_updates == 0u &&
                 metrics.unknown_records == 0u && metrics.records_rejected == 0u,
             "HWSTATUSA and AGCA should be treated as known parsed telemetry even without runtime updates");
  ctx.Expect(session.current_state().fix_type == GnssFixType::kUnknown &&
                 !session.current_state().timestamp_ns.has_value(),
             "HWSTATUSA and AGCA should not modify the aggregated runtime state");
}

void TestBinaryBestNavAndPvtslnRouting(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedBytes(BuildUnicoreBinaryFrame(2118u, MakeBestNavBPayload()), 9100);
  session.FeedBytes(BuildUnicoreBinaryFrame(1021u, MakePvtslnBPayload()), 9200);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.binary_frames_seen == 2u &&
                 metrics.records_parsed == 2u && metrics.runtime_updates == 2u,
             "BESTNAVB and PVTSLNB should route through the binary Unicore session path");
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(9200) &&
                 state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFixed &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed),
             "PVTSLNB should update the final fix and RTK state");
  ctx.Expect(state.latitude_deg.has_value() &&
                 NearlyEqual(*state.latitude_deg, 40.07898130522) &&
                 state.longitude_deg.has_value() &&
                 NearlyEqual(*state.longitude_deg, 116.23663134427) &&
                 state.altitude_m.has_value() &&
                 NearlyEqual(*state.altitude_m, 60.5060, 1e-4),
             "binary Unicore runtime routing should update coordinates and altitude");
  ctx.Expect(state.horizontal_accuracy_m.has_value() &&
                 std::fabs(*state.horizontal_accuracy_m - 0.18f) < 1e-6f &&
                 state.vertical_accuracy_m.has_value() &&
                 std::fabs(*state.vertical_accuracy_m - 0.2f) < 1e-6f &&
                 state.correction_age_s.has_value() &&
                 std::fabs(*state.correction_age_s - 0.9f) < 1e-6f &&
                 state.heading_deg.has_value() &&
                 NearlyEqual(*state.heading_deg, 182.25, 1e-6) &&
                 state.hdop.has_value() &&
                 std::fabs(*state.hdop - 0.684f) < 1e-6f,
             "PVTSLNB should carry accuracy, correction age, heading, and HDOP");
}

void TestStartupBinaryResyncSuppressesFirstMalformedFrame(TestContext& ctx)
{
  UnicoreSession session;
  auto invalid_frame = BuildUnicoreBinaryFrame(2118u, MakeBestNavBPayload());
  invalid_frame.back() ^= 0xFFu;

  session.FeedBytes(invalid_frame, 9300);
  session.FeedBytes(BuildUnicoreBinaryFrame(2118u, MakeBestNavBPayload()), 9400);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.binary_frames_seen == 1u &&
                 metrics.malformed_frames == 0u &&
                 metrics.records_parsed == 1u &&
                 metrics.runtime_updates == 1u,
             "the first malformed binary frame before sync should be suppressed once");
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(9400) &&
                 state.fix_valid &&
                 state.latitude_deg.has_value() &&
                 NearlyEqual(*state.latitude_deg, 40.0789588272),
             "a valid binary frame after startup resync should still update runtime state");
}

void TestStartupAsciiResyncSuppressesFirstMalformedLine(TestContext& ctx)
{
  UnicoreSessionConfig config;
  config.max_frame_length_bytes = 8u;
  UnicoreSession session(config);

  session.FeedString("#TOO_LONG_LINE");
  session.FeedString("#A;\r\n", 9500);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.malformed_lines == 0u &&
                 metrics.lines_seen == 1u &&
                 metrics.ascii_records_seen == 1u &&
                 metrics.unknown_records == 1u,
             "the first malformed ASCII fragment before sync should be suppressed once");
}

void TestMalformedBinaryFrameAfterSyncCounts(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedBytes(BuildUnicoreBinaryFrame(2118u, MakeBestNavBPayload()), 9600);

  auto invalid_frame = BuildUnicoreBinaryFrame(2118u, MakeBestNavBPayload());
  invalid_frame.back() ^= 0x55u;
  session.FeedBytes(invalid_frame, 9700);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.binary_frames_seen == 1u &&
                 metrics.malformed_frames == 1u &&
                 metrics.records_parsed == 1u &&
                 metrics.runtime_updates == 1u,
             "malformed binary frames after initial sync should still be counted");
}

void TestFinalizeAndReset(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString("#BESTNAVA,97,GPS,FINE,2294");
  session.Finalize();

  ctx.Expect(session.metrics().malformed_lines == 1u,
             "finalizing a truncated trailing line should count a malformed line");

  session.FeedString(kBestNavLine, 8000);
  session.Reset();
  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.bytes_seen == 0u && metrics.lines_seen == 0u &&
                 metrics.records_parsed == 0u && metrics.runtime_updates == 0u &&
                 metrics.malformed_lines == 0u,
             "reset should clear session metrics");
  ctx.Expect(state.fix_type == GnssFixType::kUnknown && !state.fix_valid &&
                 !state.latitude_deg.has_value(),
             "reset should clear the aggregated runtime state");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestBestNavUpdatesRuntimeState(ctx);
  TestPvtslnUpdatesHeading(ctx);
  TestRtkStatusUpdatesDualAntenna(ctx);
  TestSatsInfoUpdatesTrackedAndCn0(ctx);
  TestBestSatUpdatesTrackedAndUsedOnly(ctx);
  TestJammingStatusUpdatesRuntimeState(ctx);
  TestRtcmStatusParsesWithoutRuntimeUpdate(ctx);
  TestUnknownAndMalformedRecords(ctx);
  TestPartialChunksAcrossFeeds(ctx);
  TestHardwareAndAgcRecordsCountAsParsedWithoutRuntimeUpdate(ctx);
  TestBinaryBestNavAndPvtslnRouting(ctx);
  TestStartupBinaryResyncSuppressesFirstMalformedFrame(ctx);
  TestStartupAsciiResyncSuppressesFirstMalformedLine(ctx);
  TestMalformedBinaryFrameAfterSyncCounts(ctx);
  TestFinalizeAndReset(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver Unicore session tests passed\n";
  return EXIT_SUCCESS;
}
