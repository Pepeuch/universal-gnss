#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"

namespace
{

using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss_driver::ReceiverSession;
using universal_gnss_driver::ReceiverSessionConfig;
using universal_gnss_driver::ReceiverSessionKind;

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

std::string BuildUnicoreAsciiFrame(const std::string& frame_without_crc)
{
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

std::vector<std::uint8_t> BuildNmeaSentence(const std::string& payload)
{
  std::vector<std::uint8_t> bytes;
  bytes.push_back(static_cast<std::uint8_t>('$'));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.push_back(static_cast<std::uint8_t>('*'));

  const std::uint8_t checksum = universal_gnss_protocols::ComputeNmeaChecksum(payload);
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

std::vector<std::uint8_t> BuildUbxFrame(const std::uint8_t class_id,
                                        const std::uint8_t message_id,
                                        const std::vector<std::uint8_t>& payload)
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
  bytes.push_back(checksum.ck_a);
  bytes.push_back(checksum.ck_b);
  return bytes;
}

std::vector<std::uint8_t> BuildRtcmFrame(const std::uint16_t message_type)
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

  const std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

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
  std::vector<std::uint8_t> frame = {
      universal_gnss_protocols::kUnicoreBinarySync1,
      universal_gnss_protocols::kUnicoreBinarySync2,
      universal_gnss_protocols::kUnicoreBinarySync3,
      97u,
  };
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

std::vector<std::uint8_t> MakePvtslnBinaryPayload()
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
  payload[21u] = 0x01u;
  payload[23u] = 18u;
  WriteLeI4(payload, 24u, 231234567);
  WriteLeI4(payload, 28u, 485678901);
  WriteLeI4(payload, 32u, 123450);
  WriteLeI4(payload, 36u, 120000);
  WriteLeU4(payload, 40u, 250u);
  WriteLeU4(payload, 44u, 500u);
  return payload;
}

const std::string kBestNavLine = BuildUnicoreAsciiFrame(
    "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
    "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
    "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
    "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123");

void TestExplicitUbloxMode(TestContext& ctx)
{
  ReceiverSession session(ReceiverSessionConfig{ReceiverSessionKind::kUblox});
  session.FeedBytes(BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload()), 1000);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.selected_session_kind == std::optional<ReceiverSessionKind>(
                                                  ReceiverSessionKind::kUblox) &&
                 metrics.selection_locked,
             "explicit u-blox mode should start selected and locked");
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(1000) &&
                 state.fix_valid && state.fix_type == GnssFixType::kFix,
             "explicit u-blox mode should route NAV-PVT into runtime state");
  ctx.Expect(metrics.runtime_updates == 1u &&
                 session.ublox_metrics().ubx_frames_seen == 1u &&
                 session.unicore_metrics().records_parsed == 0u,
             "explicit u-blox mode should expose child metrics cleanly");
}

void TestExplicitUnicoreMode(TestContext& ctx)
{
  ReceiverSession session(ReceiverSessionConfig{ReceiverSessionKind::kUnicore});
  session.FeedString(kBestNavLine, 2000);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.selected_session_kind == std::optional<ReceiverSessionKind>(
                                                  ReceiverSessionKind::kUnicore) &&
                 metrics.selection_locked,
             "explicit Unicore mode should start selected and locked");
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(2000) &&
                 state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFloat &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFloat),
             "explicit Unicore mode should route BESTNAVA into runtime state");
  ctx.Expect(metrics.runtime_updates == 1u &&
                 session.unicore_metrics().records_parsed == 1u &&
                 session.ublox_metrics().ubx_frames_seen == 0u,
             "explicit Unicore mode should expose child metrics cleanly");
}

void TestExplicitNmeaMode(TestContext& ctx)
{
  ReceiverSession session(ReceiverSessionConfig{ReceiverSessionKind::kNmea});
  session.FeedBytes(
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,4,08,0.9,545.4,M,46.9,M,,"),
      2100);
  session.FeedBytes(
      BuildNmeaSentence("GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5"), 2101);
  session.FeedBytes(
      BuildNmeaSentence("GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"),
      2102);
  session.FeedBytes(
      BuildNmeaSentence("GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,0.6,1.1"), 2103);
  session.FeedBytes(
      BuildNmeaSentence("GPVTG,054.7,T,034.4,M,005.5,N,010.2,K,A"), 2104);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.selected_session_kind == std::optional<ReceiverSessionKind>(
                                                  ReceiverSessionKind::kNmea) &&
                 metrics.selection_locked,
             "explicit NMEA mode should start selected and locked");
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kFix &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed) &&
                 state.hdop == std::optional<float>(1.0f) &&
                 state.vdop == std::optional<float>(1.5f) &&
                 state.satellites_visible == std::optional<std::uint16_t>(8u) &&
                 state.horizontal_accuracy_m == std::optional<float>(0.6f) &&
                 !state.heading_deg.has_value(),
             "explicit NMEA mode should route runtime-mapped NMEA sentences, including GGA-derived RTK fixed, without inventing VTG heading");
  ctx.Expect(session.nmea_metrics().semantic_only_records == 1u &&
                 session.nmea_metrics().records_parsed == 5u,
             "explicit NMEA mode should still parse VTG semantically");
}

void TestAutoModeSelectsUblox(TestContext& ctx)
{
  ReceiverSession session;
  auto nmea = BuildNmeaSentence(
      "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  const auto ubx = BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload());

  session.FeedBytes(nmea, 3000);
  ctx.Expect(!session.metrics().selected_session_kind.has_value() &&
                 !session.metrics().selection_locked,
             "auto mode should stay undecided on NMEA-only input");

  session.FeedBytes(ubx, 3001);
  const auto& metrics = session.metrics();
  ctx.Expect(metrics.selected_session_kind == std::optional<ReceiverSessionKind>(
                                                  ReceiverSessionKind::kUblox) &&
                 metrics.selection_locked,
             "auto mode should select u-blox once UBX evidence appears");
  ctx.Expect(session.current_state().fix_valid &&
                 session.current_state().fix_type == GnssFixType::kFix,
             "auto-selected u-blox session should expose UBX runtime state");
}

void TestAutoModeSelectsUnicore(TestContext& ctx)
{
  ReceiverSession session;
  session.FeedString(kBestNavLine, 4000);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.selected_session_kind == std::optional<ReceiverSessionKind>(
                                                  ReceiverSessionKind::kUnicore) &&
                 metrics.selection_locked,
             "auto mode should select Unicore on Unicore ASCII input");
  ctx.Expect(session.current_state().fix_valid &&
                 session.current_state().fix_type == GnssFixType::kRtkFloat,
             "auto-selected Unicore session should expose Unicore runtime state");
}

void TestAutoModeSelectsNmeaWhenEnabled(TestContext& ctx)
{
  ReceiverSessionConfig config;
  config.allow_generic_nmea_auto_detect = true;
  ReceiverSession session(config);
  session.FeedBytes(
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),
      4250);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.selected_session_kind == std::optional<ReceiverSessionKind>(
                                                  ReceiverSessionKind::kNmea) &&
                 metrics.selection_locked,
             "auto mode should select generic NMEA only when the fallback is explicitly enabled");
  ctx.Expect(session.current_state().fix_valid &&
                 session.current_state().fix_type == GnssFixType::kFix,
             "generic NMEA auto-selection should expose runtime state once enabled");
}

void TestAutoModeSelectsUnicoreBinary(TestContext& ctx)
{
  ReceiverSession session;
  session.FeedBytes(BuildUnicoreBinaryFrame(1021u, MakePvtslnBinaryPayload()), 4500);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.selected_session_kind == std::optional<ReceiverSessionKind>(
                                                  ReceiverSessionKind::kUnicore) &&
                 metrics.selection_locked,
             "auto mode should select Unicore on valid Unicore binary input");
  ctx.Expect(session.current_state().fix_valid &&
                 session.current_state().fix_type == GnssFixType::kRtkFixed &&
                 session.current_state().heading_deg == std::optional<double>(182.25),
             "auto-selected Unicore binary session should expose binary runtime state");
  ctx.Expect(session.unicore_metrics().binary_frames_seen == 1u,
             "binary Unicore routing should surface child binary frame metrics");
}

void TestRtcmOnlyStaysUndecided(TestContext& ctx)
{
  ReceiverSession session;
  session.FeedBytes(BuildRtcmFrame(1005u), 5000);

  ctx.Expect(!session.metrics().selected_session_kind.has_value() &&
                 !session.metrics().selection_locked,
             "RTCM-only input should not select a receiver vendor");
  ctx.Expect(!session.current_state().fix_valid &&
                 session.current_state().fix_type == GnssFixType::kUnknown,
             "RTCM-only input should not invent runtime state");
}

void TestFinalizeAndReset(TestContext& ctx)
{
  ReceiverSession session(ReceiverSessionConfig{ReceiverSessionKind::kUblox});
  const auto ubx = BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload());
  session.FeedBytes(ubx.data(), 8u, 6000);
  session.Finalize();

  ctx.Expect(session.metrics().malformed_records == 1u &&
                 session.ublox_metrics().malformed_frames == 1u,
             "finalize should propagate truncation handling to the selected child session");

  session.Reset();
  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.bytes_seen == 0u &&
                 metrics.runtime_updates == 0u &&
                 metrics.malformed_records == 0u &&
                 metrics.selected_session_kind == std::optional<ReceiverSessionKind>(
                                                  ReceiverSessionKind::kUblox) &&
                 metrics.selection_locked,
             "reset should clear metrics and restore explicit-mode selection");
  ctx.Expect(state.fix_type == GnssFixType::kUnknown &&
                 !state.fix_valid && !state.latitude_deg.has_value(),
             "reset should clear runtime state");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestExplicitUbloxMode(ctx);
  TestExplicitUnicoreMode(ctx);
  TestExplicitNmeaMode(ctx);
  TestAutoModeSelectsUblox(ctx);
  TestAutoModeSelectsUnicore(ctx);
  TestAutoModeSelectsNmeaWhenEnabled(ctx);
  TestAutoModeSelectsUnicoreBinary(ctx);
  TestRtcmOnlyStaysUndecided(ctx);
  TestFinalizeAndReset(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver session tests passed\n";
  return EXIT_SUCCESS;
}
