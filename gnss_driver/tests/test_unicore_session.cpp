#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/unicore_session.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssBaselineSolutionStatus;
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

std::string WithUnicoreAsciiCrc(const std::string& frame_without_crc)
{
  if (frame_without_crc.empty() || frame_without_crc.front() != '#')
  {
    std::cerr << "FAILED: invalid Unicore ASCII test vector\n";
    std::exit(EXIT_FAILURE);
  }

  const auto crc = universal_gnss_protocols::ComputeUnicoreBinaryCrc32(
      reinterpret_cast<const std::uint8_t*>(frame_without_crc.data() + 1u),
      frame_without_crc.size() - 1u);

  std::ostringstream stream;
  stream << frame_without_crc
         << '*'
         << std::hex
         << std::nouppercase
         << std::setw(8)
         << std::setfill('0')
         << crc
         << "\r\n";
  return stream.str();
}

bool NearlyEqual(const double lhs, const double rhs, const double tolerance = 1e-6)
{
  return std::fabs(lhs - rhs) <= tolerance;
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

const std::string kBestNavLine = WithUnicoreAsciiCrc(
    "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
    "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
    "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
    "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123");

const std::string kPvtslnLine = WithUnicoreAsciiCrc(
    "#PVTSLNA,97,GPS,FINE,2190,364536000,0,0,18,13;"
    "NARROW_INT,60.5060,40.07898130522,116.23663134427,0.2000,0.1500,0.1800,0.9000,"
    "SINGLE,60.5060,40.07898130522,116.23663134427,4.3353,46,28,46,28,0.0009,-0.0031,-0.0032,"
    "SOL_COMPUTED,1.5000,182.2500,0.1000,28,25,12,8,2.1753,1.3480,0.6840,1.8392,1.7072,5.0,"
    "28,25,26");

const std::string kRtkStatusLine = WithUnicoreAsciiCrc(
    "#RTKSTATUSA,97,GPS,FINE,2190,365354000,0,0,18,1;"
    "0,0,0,0,0,0,0,0,0,0,0,NARROW_INT,5,0,1,12,0");

const std::string kSatsInfoLine = WithUnicoreAsciiCrc(
    "#SATSINFOA,96,GPS,FINE,2215,367199000,0,0,18,16;"
    "3,2,0,0,0,63,"
    "2,302,51,0,45,0,2,0,42,9,2,"
    "4,48,17,0,37,0,3,0,43,14,3,0,39,9,3,"
    "5,225,14,1,50,0,1");

const std::string kBestSatLine = WithUnicoreAsciiCrc(
    "#BESTSATA,79,GPS,FINE,2203,226245800,0,0,18,22;"
    "4,GPS,2,GOOD,00000013,GLONASS,2-4,GOOD,00000010,GALILEO,5,GOOD,00000001,BEIDOU,20,GOOD,00000000");

const std::string kJamStatusLine = WithUnicoreAsciiCrc(
    "#JAMSTATUSA,97,GPS,FINE,2190,365412000,0,0,18,14;SINGLE,120,2,0,0");

const std::string kRtcmStatusLine = WithUnicoreAsciiCrc(
    "#RTCMSTATUSA,76,GPS,FINE,2219,392572000,0,0,18,187;"
    "1124,21186,0,21,0,6,11,0,0,21");

const std::string kHwStatusLine = WithUnicoreAsciiCrc(
    "#HWSTATUSA,97,GPS,FINE,2221,111183000,0,0,18,15;66807,0.920,1.020,0.908,1,0.693,0.0,0x00,0,0x0377,0,0");

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
  WriteLittleEndian32(payload, 40u, 16u);
  WriteLittleEndianFloat32(payload, 44u, 60.5060f);
  WriteLittleEndianFloat64(payload, 48u, 40.07898130522);
  WriteLittleEndianFloat64(payload, 56u, 116.23663134427);
  WriteLittleEndianFloat32(payload, 64u, -8.4923f);
  payload[68u] = 46u;
  payload[69u] = 28u;
  payload[70u] = 46u;
  payload[71u] = 28u;
  WriteLittleEndianFloat64(payload, 72u, 0.0009);
  WriteLittleEndianFloat64(payload, 80u, -0.0031);
  WriteLittleEndianFloat64(payload, 88u, 0.0032);
  WriteLittleEndian32(payload, 96u, 0u);
  WriteLittleEndianFloat32(payload, 100u, 1.5000f);
  WriteLittleEndianFloat32(payload, 104u, 182.2500f);
  WriteLittleEndianFloat32(payload, 108u, 0.1000f);
  payload[112u] = 28u;
  payload[113u] = 25u;
  payload[114u] = 12u;
  payload[115u] = 8u;
  WriteLittleEndianFloat32(payload, 116u, 2.1753f);
  WriteLittleEndianFloat32(payload, 120u, 1.3480f);
  WriteLittleEndianFloat32(payload, 124u, 0.6840f);
  WriteLittleEndianFloat32(payload, 128u, 1.8392f);
  WriteLittleEndianFloat32(payload, 132u, 1.7072f);
  WriteLittleEndianFloat32(payload, 136u, 5.0f);
  payload[140u] = 28u;
  payload[141u] = 25u;
  payload[142u] = 26u;
  return payload;
}

std::vector<std::uint8_t> MakeBestNavBPayloadWithEmbeddedAsciiSync()
{
  auto payload = MakeBestNavBPayload();
  constexpr const char* kEmbeddedNoise = "#FOOBARA,97,GPS,FINE,1,2,0,0,0,0;payload\r\n";
  const std::string text = kEmbeddedNoise;
  const std::size_t offset = 72u;
  for (std::size_t i = 0u; i < text.size() && offset + i < payload.size(); ++i)
  {
    payload[offset + i] = static_cast<std::uint8_t>(text[i]);
  }
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
                 HasCapability(state, GnssCapability::kDualAntennaBaseline) &&
                 HasValueAvailable(state, GnssCapability::kDualAntennaBaseline) &&
                 HasValueAvailable(state, GnssCapability::kBaselineSolutionStatus) &&
                 state.heading_deg == std::optional<double>(182.25) &&
                 state.dual_antenna_baseline == std::optional<bool>(true) &&
                 state.baseline_solution_status ==
                     std::optional<GnssBaselineSolutionStatus>(
                         GnssBaselineSolutionStatus::kComputed) &&
                 state.dual_antenna_heading == std::optional<bool>(true),
             "PVTSLNA should update compatibility heading plus canonical baseline state");
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
                 HasCapability(state, GnssCapability::kDualAntennaBaseline) &&
                 HasValueAvailable(state, GnssCapability::kDualAntennaHeading) &&
                 HasValueAvailable(state, GnssCapability::kDualAntennaBaseline) &&
                 state.dual_antenna_heading == std::optional<bool>(true) &&
                 state.dual_antenna_baseline == std::optional<bool>(true),
             "RTKSTATUSA should update compatibility and canonical baseline state");
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

void TestNmeaGsvUpdatesVisibleAndCn0AcrossTalkers(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedBytes(
      BuildNmeaSentence("GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"),
      5800);
  session.FeedBytes(
      BuildNmeaSentence("GPGSV,2,2,08,15,05,300,37,18,30,045,40,20,15,180,38,22,20,270,36"),
      5801);
  session.FeedBytes(
      BuildNmeaSentence("GLGSV,1,1,06,65,45,123,35,66,30,200,34,67,20,250,33,68,15,300,32"),
      5802);

  const auto& state = session.current_state();
  const auto& metrics = session.metrics();
  ctx.Expect(metrics.records_parsed == 3u &&
                 metrics.runtime_observations == 3u &&
                 metrics.runtime_updates == 3u,
             "mixed Unicore NMEA GSV sentences should count as parsed runtime observations");
  ctx.Expect(HasCapability(state, GnssCapability::kSatellitesVisible) &&
                 HasCapability(state, GnssCapability::kSatellitesTracked) &&
                 HasCapability(state, GnssCapability::kMeanCn0) &&
                 HasCapability(state, GnssCapability::kMaxCn0) &&
                 HasValueAvailable(state, GnssCapability::kSatellitesVisible) &&
                 HasValueAvailable(state, GnssCapability::kSatellitesTracked) &&
                 HasValueAvailable(state, GnssCapability::kMeanCn0) &&
                 HasValueAvailable(state, GnssCapability::kMaxCn0) &&
                 state.satellites_tracked == std::optional<std::uint16_t>(12u) &&
                 state.satellites_visible == std::optional<std::uint16_t>(14u) &&
                 state.mean_cn0_db_hz.has_value() &&
                 std::fabs(*state.mean_cn0_db_hz - 37.5f) < 1e-6f &&
                 state.max_cn0_db_hz == std::optional<float>(43.0f),
             "GSV routing should aggregate conservative tracked counts, visible satellites, and CN0 across recent talkers");
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

void TestNmeaFallbackDoesNotOverrideRichUnicoreState(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kBestNavLine, 6000);
  session.FeedBytes(
      BuildNmeaSentence("GNGGA,123519,4807.111,N,01131.999,E,1,08,1.5,100.1,M,46.9,M,,"),
      6001);
  session.FeedBytes(
      BuildNmeaSentence("GPGST,123519.00,1.2,0.8,0.7,45.0,9.9,8.8,7.7"),
      6002);

  const auto& state = session.current_state();
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFloat &&
                 state.latitude_deg == std::optional<double>(40.0789588272) &&
                 state.longitude_deg == std::optional<double>(116.2365102982) &&
                 state.altitude_m == std::optional<double>(65.8312) &&
                 state.horizontal_accuracy_m.has_value() &&
                 std::fabs(*state.horizontal_accuracy_m - 1.2221f) < 1e-6f &&
                 state.vertical_accuracy_m.has_value() &&
                 std::fabs(*state.vertical_accuracy_m - 2.1970f) < 1e-6f,
             "NMEA fallback sentences should not overwrite richer Unicore fix, position, or accuracy");
}

void TestNmeaFallbackProvidesPositionAndAccuracyWhenUnicoreStateIsMissing(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedBytes(
      BuildNmeaSentence("GNGGA,123519,4807.038,N,01131.000,E,2,08,0.9,545.4,M,46.9,M,,"),
      6100);
  session.FeedBytes(
      BuildNmeaSentence("GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,0.6,1.1"),
      6101);

  const auto& state = session.current_state();
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kFix &&
                 state.latitude_deg.has_value() &&
                 state.longitude_deg.has_value() &&
                 state.altitude_m == std::optional<double>(545.4) &&
                 state.hdop == std::optional<float>(0.9f) &&
                 state.satellites_used == std::optional<std::uint16_t>(8u) &&
                 state.horizontal_accuracy_m == std::optional<float>(0.6f) &&
                 state.vertical_accuracy_m == std::optional<float>(1.1f),
             "NMEA fallback should populate fix and accuracy only when Unicore state is still missing");
}

void TestMixedNmeaSatelliteCountsStayAuthoritativeOverPositionTail(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedBytes(
      BuildNmeaSentence("GNGGA,123519,4807.038,N,01131.000,E,2,17,0.9,545.4,M,46.9,M,,"),
      6200);
  session.FeedBytes(
      BuildNmeaSentence("GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"),
      6201);
  session.FeedBytes(
      BuildNmeaSentence("GPGSV,2,2,08,15,05,300,37,18,30,045,40,20,15,180,38,22,20,270,36"),
      6202);
  session.FeedBytes(
      BuildNmeaSentence("GLGSV,1,1,06,65,45,123,35,66,30,200,34,67,20,250,33,68,15,300,32"),
      6203);
  session.FeedString(kBestNavLine, 6204);
  session.FeedString(kPvtslnLine, 6205);

  const auto& state = session.current_state();
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFixed &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed) &&
                 state.latitude_deg == std::optional<double>(40.07898130522) &&
                 state.longitude_deg == std::optional<double>(116.23663134427),
             "later PVTSLNA should still update the rich fix and position fields");
  ctx.Expect(state.satellites_used == std::optional<std::uint16_t>(17u) &&
                 state.satellites_tracked == std::optional<std::uint16_t>(17u) &&
                 state.satellites_visible == std::optional<std::uint16_t>(17u),
             "recent mixed NMEA GGA/GSV satellite counts should remain physically coherent over position-message tails");
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
  session.FeedString(WithUnicoreAsciiCrc("#FOOBARA,97,GPS,FINE,1,2,0,0,0,0;payload"));
  session.FeedString(WithUnicoreAsciiCrc(
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,SINGLE,not_a_latitude,116.2365102982,65.8312"));

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
                 state.dual_antenna_baseline == std::optional<bool>(true) &&
                 state.baseline_azimuth_deg == std::optional<float>(182.25f) &&
                 state.baseline_pitch_deg == std::optional<float>(0.1f) &&
                 state.baseline_length_m == std::optional<float>(1.5f) &&
                 state.hdop.has_value() &&
                 std::fabs(*state.hdop - 0.684f) < 1e-6f,
             "PVTSLNB should carry accuracy, baseline geometry, correction age, heading, and HDOP");
}

void TestUnknownBinaryFrameCountsWithoutRuntimeUpdate(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedBytes(BuildUnicoreBinaryFrame(9999u, std::vector<std::uint8_t>(8u, 0x42u)), 9250);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.binary_frames_seen == 1u &&
                 metrics.unknown_records == 1u &&
                 metrics.records_parsed == 0u &&
                 metrics.records_rejected == 0u &&
                 metrics.runtime_updates == 0u,
             "valid but unsupported Unicore binary message ids should remain unknown and must not be decoded as runtime state");
  ctx.Expect(session.current_state().fix_type == GnssFixType::kUnknown &&
                 !session.current_state().timestamp_ns.has_value(),
             "unknown Unicore binary frames should not invent runtime state");
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
  config.max_frame_length_bytes = 24u;
  UnicoreSession session(config);

  session.FeedString("#TOO_LONG_LINE_EXCEEDS_LIMIT");
  session.FeedString(WithUnicoreAsciiCrc("#X;"), 9500);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.malformed_lines == 0u,
             "startup ASCII resync should suppress the first malformed line");
  ctx.Expect(metrics.lines_seen == 1u,
             "startup ASCII resync should still count the later unknown line");
  ctx.Expect(metrics.ascii_records_seen == 1u,
             "startup ASCII resync should still expose the later ASCII record");
  ctx.Expect(metrics.unknown_records == 1u,
             "startup ASCII resync should route the later line as an unknown record");
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

void TestStrayBinarySyncCanFallbackIntoAsciiFrame(TestContext& ctx)
{
  UnicoreSession session;
  std::string bytes;
  bytes.push_back(static_cast<char>(universal_gnss_protocols::kUnicoreBinarySync1));
  bytes += kBestNavLine;
  session.FeedBytes(
      reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), 9800);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.lines_seen == 1u &&
                 metrics.ascii_records_seen == 1u &&
                 metrics.records_parsed == 1u &&
                 metrics.runtime_updates == 1u &&
                 metrics.unknown_records == 0u &&
                 metrics.records_rejected == 0u &&
                 metrics.malformed_frames == 0u,
             "a stray binary sync byte ahead of a real ASCII frame should not hide the ASCII record");
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(9800) &&
                 state.fix_valid &&
                 state.latitude_deg == std::optional<double>(40.0789588272),
             "binary fallback should still preserve the first-byte timestamp and runtime state");
}

void TestBinaryPayloadDoesNotLeakIntoAsciiFramer(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedBytes(BuildUnicoreBinaryFrame(2118u, MakeBestNavBPayloadWithEmbeddedAsciiSync()),
                    9900);
  session.FeedString(kBestNavLine, 10000);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.binary_frames_seen == 1u &&
                 metrics.lines_seen == 1u &&
                 metrics.ascii_records_seen == 1u &&
                 metrics.records_parsed == 2u &&
                 metrics.runtime_updates == 2u &&
                 metrics.unknown_records == 0u &&
                 metrics.records_rejected == 0u &&
                 metrics.malformed_lines == 0u &&
                 metrics.malformed_frames == 0u,
             "binary payload bytes that look like ASCII sync should stay isolated inside the binary framer");
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(10000) &&
                 state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFloat &&
                 state.latitude_deg == std::optional<double>(40.0789588272),
             "mixed binary and ASCII runtime records should still converge on the final parsed state");
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
  TestNmeaGsvUpdatesVisibleAndCn0AcrossTalkers(ctx);
  TestBestSatUpdatesTrackedAndUsedOnly(ctx);
  TestNmeaFallbackDoesNotOverrideRichUnicoreState(ctx);
  TestNmeaFallbackProvidesPositionAndAccuracyWhenUnicoreStateIsMissing(ctx);
  TestMixedNmeaSatelliteCountsStayAuthoritativeOverPositionTail(ctx);
  TestJammingStatusUpdatesRuntimeState(ctx);
  TestRtcmStatusParsesWithoutRuntimeUpdate(ctx);
  TestUnknownAndMalformedRecords(ctx);
  TestPartialChunksAcrossFeeds(ctx);
  TestHardwareAndAgcRecordsCountAsParsedWithoutRuntimeUpdate(ctx);
  TestBinaryBestNavAndPvtslnRouting(ctx);
  TestUnknownBinaryFrameCountsWithoutRuntimeUpdate(ctx);
  TestStartupBinaryResyncSuppressesFirstMalformedFrame(ctx);
  TestStartupAsciiResyncSuppressesFirstMalformedLine(ctx);
  TestMalformedBinaryFrameAfterSyncCounts(ctx);
  TestStrayBinarySyncCanFallbackIntoAsciiFrame(ctx);
  TestBinaryPayloadDoesNotLeakIntoAsciiFramer(ctx);
  TestFinalizeAndReset(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver Unicore session tests passed\n";
  return EXIT_SUCCESS;
}
