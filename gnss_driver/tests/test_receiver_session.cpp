#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
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

constexpr const char* kBestNavLine =
    "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
    "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
    "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
    "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n";

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
  TestAutoModeSelectsUblox(ctx);
  TestAutoModeSelectsUnicore(ctx);
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
