#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_driver/receiver_session_runner.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_transport/memory_stream.hpp"
#include "universal_gnss_transport/transport_error.hpp"
#include "universal_gnss_transport/transport_status.hpp"

namespace
{

using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss_driver::ReceiverSession;
using universal_gnss_driver::ReceiverSessionConfig;
using universal_gnss_driver::ReceiverSessionKind;
using universal_gnss_driver::ReceiverSessionRunner;
using universal_gnss_driver::ReceiverSessionRunnerConfig;
using universal_gnss_transport::MemoryByteSource;
using universal_gnss_transport::TransportError;
using universal_gnss_transport::TransportStatus;

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

void TestRunUntilEofWithMixedUbloxStream(TestContext& ctx)
{
  std::vector<std::uint8_t> stream;
  const auto gga = BuildNmeaSentence(
      "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  const auto ubx = BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload());
  const auto rtcm = BuildRtcmFrame(1077u);
  stream.insert(stream.end(), gga.begin(), gga.end());
  stream.insert(stream.end(), ubx.begin(), ubx.end());
  stream.insert(stream.end(), rtcm.begin(), rtcm.end());

  MemoryByteSource source(stream);
  ReceiverSession session(ReceiverSessionConfig{ReceiverSessionKind::kUblox});
  ReceiverSessionRunner runner(source, session, ReceiverSessionRunnerConfig{7u});
  runner.RunUntilEof();

  const auto& runner_metrics = runner.metrics();
  const auto& session_metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(runner_metrics.bytes_read == stream.size() &&
                 runner_metrics.chunks_read > 1u &&
                 runner_metrics.eof_seen,
             "runner should drain the full mixed u-blox stream in multiple chunks");
  ctx.Expect(session_metrics.selected_session_kind == std::optional<ReceiverSessionKind>(
                                                      ReceiverSessionKind::kUblox) &&
                 state.fix_valid &&
                 state.fix_type == GnssFixType::kFix,
             "mixed u-blox stream should update the routed receiver session");
  ctx.Expect(runner_metrics.runtime_updates_observed == session_metrics.runtime_updates &&
                 session.ublox_metrics().rtcm_frames_seen == 1u,
             "runner should observe runtime update deltas and keep RTCM metadata");
}

void TestRunUntilEofWithUnicoreStream(TestContext& ctx)
{
  const std::vector<std::uint8_t> stream(kBestNavLine.begin(), kBestNavLine.end());
  MemoryByteSource source(stream);
  ReceiverSession session(ReceiverSessionConfig{ReceiverSessionKind::kUnicore});
  ReceiverSessionRunner runner(source, session, ReceiverSessionRunnerConfig{9u});
  runner.RunUntilEof();

  const auto& state = session.current_state();
  ctx.Expect(runner.metrics().eof_seen &&
                 runner.metrics().runtime_updates_observed == 1u,
             "runner should drain a Unicore stream to EOF and observe one runtime update");
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFloat &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFloat),
             "Unicore BESTNAVA stream should update the runtime state");
}

void TestChunkBoundarySplittingAndAutoSelection(TestContext& ctx)
{
  const auto ubx = BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload());
  MemoryByteSource source(ubx);
  ReceiverSession session;
  ReceiverSessionRunner runner(source, session, ReceiverSessionRunnerConfig{1u});
  runner.RunUntilEof();

  ctx.Expect(session.metrics().selected_session_kind == std::optional<ReceiverSessionKind>(
                                                       ReceiverSessionKind::kUblox) &&
                 session.current_state().fix_valid,
             "single-byte chunking should still allow auto-detection and runtime updates");
  ctx.Expect(runner.metrics().chunks_read == ubx.size(),
             "single-byte chunking should produce one chunk per byte read");
}

void TestReadErrorHandling(TestContext& ctx)
{
  MemoryByteSource source({0x01u, 0x02u});
  source.InjectNextReadError(TransportError::kReadFailure);
  ReceiverSession session(ReceiverSessionConfig{ReceiverSessionKind::kUblox});
  ReceiverSessionRunner runner(source, session);

  const bool keep_running = runner.StepOnce();
  ctx.Expect(!keep_running &&
                 runner.metrics().read_errors == 1u &&
                 !runner.metrics().eof_seen &&
                 runner.metrics().last_status == TransportStatus::kError &&
                 runner.metrics().last_error == TransportError::kReadFailure,
             "runner should stop and record transport read errors");
  ctx.Expect(session.metrics().runtime_updates == 0u,
             "read errors should not invent runtime updates");
}

void TestFinalizePropagationOnEof(TestContext& ctx)
{
  const auto ubx = BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload());
  MemoryByteSource source(std::vector<std::uint8_t>(ubx.begin(), ubx.begin() + 8));
  ReceiverSession session(ReceiverSessionConfig{ReceiverSessionKind::kUblox});
  ReceiverSessionRunner runner(source, session, ReceiverSessionRunnerConfig{32u, true, true, false});
  runner.RunUntilEof();

  ctx.Expect(runner.metrics().eof_seen &&
                 session.metrics().malformed_records == 1u &&
                 session.ublox_metrics().malformed_frames == 1u,
             "EOF-triggered finalization should propagate truncation handling to the child session");
}

void TestResetMetrics(TestContext& ctx)
{
  const auto ubx = BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload());
  MemoryByteSource source(ubx);
  ReceiverSession session(ReceiverSessionConfig{ReceiverSessionKind::kUblox});
  ReceiverSessionRunner runner(source, session);
  runner.RunUntilEof();
  runner.ResetMetrics();

  ctx.Expect(runner.metrics().bytes_read == 0u &&
                 runner.metrics().chunks_read == 0u &&
                 !runner.metrics().eof_seen &&
                 runner.metrics().runtime_updates_observed == 0u &&
                 runner.metrics().last_status == TransportStatus::kOk &&
                 runner.metrics().last_error == TransportError::kNone,
             "resetting runner metrics should clear runner counters without mutating the session");
  ctx.Expect(session.metrics().runtime_updates > 0u,
             "resetting runner metrics should not rewind receiver-session state");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestRunUntilEofWithMixedUbloxStream(ctx);
  TestRunUntilEofWithUnicoreStream(ctx);
  TestChunkBoundarySplittingAndAutoSelection(ctx);
  TestReadErrorHandling(ctx);
  TestFinalizePropagationOnEof(ctx);
  TestResetMetrics(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver session runner tests passed\n";
  return EXIT_SUCCESS;
}
