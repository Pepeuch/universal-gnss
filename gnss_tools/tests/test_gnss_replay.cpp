#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_tools/gnss_replay.hpp"
#include "testdata_utils.hpp"

namespace
{

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

void Append(std::vector<std::uint8_t>& destination, const std::vector<std::uint8_t>& source)
{
  destination.insert(destination.end(), source.begin(), source.end());
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

std::vector<std::uint8_t> MakeNavSatPayload()
{
  std::vector<std::uint8_t> payload(8u + (3u * 12u), 0u);
  WriteLeU4(payload, 0u, 456000u);
  payload[4u] = 0x01u;
  payload[5u] = 3u;

  payload[8u] = 0u;
  payload[9u] = 4u;
  payload[10u] = 45u;
  payload[11u] = static_cast<std::uint8_t>(30);
  WriteLeI2(payload, 12u, 120);
  WriteLeI2(payload, 14u, 0);
  WriteLeU4(payload, 16u, 0x0000001Cu);

  payload[20u] = 2u;
  payload[21u] = 12u;
  payload[22u] = 38u;
  payload[23u] = static_cast<std::uint8_t>(15);
  WriteLeI2(payload, 24u, 220);
  WriteLeI2(payload, 26u, 0);
  WriteLeU4(payload, 28u, 0x00000004u);

  payload[32u] = 0u;
  payload[33u] = 18u;
  payload[34u] = 0u;
  payload[35u] = static_cast<std::uint8_t>(5);
  WriteLeI2(payload, 36u, 300);
  WriteLeI2(payload, 38u, 0);
  WriteLeU4(payload, 40u, 0x0000002Cu);

  return payload;
}

std::vector<std::uint8_t> BuildUnicoreLine(const std::string& line)
{
  return std::vector<std::uint8_t>(line.begin(), line.end());
}

std::vector<std::uint8_t> BuildMixedReplayStream()
{
  std::vector<std::uint8_t> stream = {0x00u, 0x13u, 0xFFu};

  Append(stream, BuildNmeaSentence(
                     "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
  Append(stream, BuildNmeaSentence(
                     "GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5"));
  Append(stream, BuildNmeaSentence(
                     "GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"));

  Append(stream, BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload()));
  Append(stream, BuildUbxFrame(0x01u, 0x35u, MakeNavSatPayload()));

  Append(stream, BuildUnicoreLine(
                     "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
                     "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
                     "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
                     "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n"));
  Append(stream, BuildUnicoreLine(
                     "#SATSINFOA,96,GPS,FINE,2215,367199000,0,0,18,16;"
                     "3,2,0,0,0,63,"
                     "2,302,51,0,45,0,2,0,42,9,2,"
                     "4,48,17,0,37,0,3,0,43,14,3,0,39,9,3,"
                     "5,225,14,1,50,0,1*abcdef12\r\n"));

  Append(stream, BuildRtcmFrame(1005u));
  Append(stream, BuildRtcmFrame(1077u));
  Append(stream, BuildUbxFrame(0x01u, 0x03u, std::vector<std::uint8_t>(16u, 0u), false));

  const auto truncated = BuildRtcmFrame(1230u);
  stream.insert(stream.end(), truncated.begin(), truncated.begin() + 4);
  return stream;
}

void TestReplayMergesMixedRuntimeState(TestContext& ctx)
{
  const auto bytes = BuildMixedReplayStream();
  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);

  ctx.Expect(result.summary.total_bytes_read == bytes.size(),
             "replay should report the total bytes read");
  ctx.Expect(result.summary.recognized_records == 10u,
             "replay should recognize all supported frames and sentences");
  ctx.Expect(result.summary.runtime_updates == 7u,
             "replay should count only semantic runtime updates");
  ctx.Expect(result.summary.malformed_events == 1u && result.summary.truncated_records == 1u,
             "replay should report the trailing truncated frame");
  ctx.Expect(result.summary.noise_bytes == 3u && result.summary.noise_spans == 1u,
             "replay should preserve leading-noise statistics");

  ctx.Expect(result.summary.counts_by_protocol.at("nmea") == 3u &&
                 result.summary.counts_by_protocol.at("ubx") == 3u &&
                 result.summary.counts_by_protocol.at("unicore") == 2u &&
                 result.summary.counts_by_protocol.at("rtcm3") == 2u,
             "replay should count records by protocol");
  ctx.Expect(result.summary.counts_by_rtcm_message_type.at(1005u) == 1u &&
                 result.summary.counts_by_rtcm_message_type.at(1077u) == 1u,
             "replay should keep RTCM message-type counts");

  ctx.Expect(result.events.size() == 10u,
             "replay should retain a per-record event timeline by default");
  ctx.Expect(result.events[0].identity == "GPGGA" && result.events[0].produced_runtime_update,
             "GGA should produce the first runtime update");
  ctx.Expect(result.events[5].identity == "BESTNAVA" && result.events[5].produced_runtime_update,
             "BESTNAVA should appear as a Unicore runtime update");
  ctx.Expect(result.events[7].identity == "1005" && !result.events[7].produced_runtime_update,
             "RTCM events should stay metadata-only for now");

  const auto& final_state = result.final_state;
  ctx.Expect(final_state.fix_valid &&
                 final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat &&
                 final_state.rtk_mode == std::optional<universal_gnss::GnssRtkMode>(
                                        universal_gnss::GnssRtkMode::kFloat),
             "final state should reflect the last Unicore RTK float position update");
  ctx.Expect(final_state.latitude_deg.has_value() && *final_state.latitude_deg == 40.0789588272 &&
                 final_state.longitude_deg.has_value() && *final_state.longitude_deg == 116.2365102982 &&
                 final_state.altitude_m.has_value() && *final_state.altitude_m == 65.8312,
             "final state should preserve BESTNAVA coordinates");
  ctx.Expect(final_state.satellites_used == std::optional<std::uint16_t>(28u) &&
                 final_state.satellites_tracked == std::optional<std::uint16_t>(3u) &&
                 final_state.satellites_visible == std::optional<std::uint16_t>(3u),
             "final state should merge used, tracked, and visible satellite counts across protocols");
  ctx.Expect(final_state.mean_cn0_db_hz == std::optional<float>(46.0f) &&
                 final_state.max_cn0_db_hz == std::optional<float>(50.0f),
             "final state should preserve the last CN0 summary update");
  ctx.Expect(final_state.correction_age_s == std::optional<float>(0.4f),
             "final state should preserve BESTNAVA differential age");
}

void TestReplaySummaryOnlyAndFormatting(TestContext& ctx)
{
  const auto bytes = BuildMixedReplayStream();
  const auto summary_only_result = universal_gnss_tools::ReplayGnssBytes(bytes, false);
  ctx.Expect(summary_only_result.events.empty(),
             "replay should omit event retention when include_events is false");
  ctx.Expect(summary_only_result.final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat,
             "summary-only replay should still compute the final runtime state");

  const auto full_result = universal_gnss_tools::ReplayGnssBytes(bytes);
  const std::string text = universal_gnss_tools::FormatGnssReplayText(full_result);
  const std::string summary = universal_gnss_tools::FormatGnssReplayText(full_result, true);
  const std::string json = universal_gnss_tools::FormatGnssReplayJson(full_result);

  ctx.Expect(text.find("proto=nmea") != std::string::npos &&
                 text.find("id=GPGGA") != std::string::npos &&
                 text.find("proto=unicore") != std::string::npos &&
                 text.find("id=BESTNAVA") != std::string::npos,
             "text output should include mixed protocol timeline entries");
  ctx.Expect(text.find("update=yes") != std::string::npos &&
                 text.find("final_state fix=rtk_float") != std::string::npos,
             "text output should include update markers and final state");
  ctx.Expect(summary.find("runtime_updates=7") != std::string::npos &&
                 summary.find("protocols nmea=3 rtcm3=2 ubx=3 unicore=2") != std::string::npos &&
                 summary.find("rtcm_types 1005=1 1077=1") != std::string::npos,
             "summary output should include aggregate counters and RTCM counts");
  ctx.Expect(json.find("\"protocol\":\"unicore\"") != std::string::npos &&
                 json.find("\"identity\":\"BESTNAVA\"") != std::string::npos &&
                 json.find("\"runtime_updates\":7") != std::string::npos &&
                 json.find("\"fix_type\":\"rtk_float\"") != std::string::npos &&
                 json.find("\"counts_by_rtcm_message_type\":{\"1005\":1,\"1077\":1}") !=
                     std::string::npos,
             "JSON output should include replay events, summary counts, and final state");
}

void TestReplayStreamInput(TestContext& ctx)
{
  const auto bytes = BuildMixedReplayStream();
  std::string input_bytes(bytes.begin(), bytes.end());
  std::istringstream input(input_bytes);

  const auto result = universal_gnss_tools::ReplayGnssStream(input);
  ctx.Expect(result.summary.recognized_records == 10u &&
                 result.summary.runtime_updates == 7u &&
                 result.final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat,
             "stream replay should match byte-vector replay");
}

void TestFileBackedReplay(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile(
      "mixed/nmea_ubx_rtcm_unicore.bin");
  const auto result = universal_gnss_tools::ReplayGnssBytes(bytes);

  ctx.Expect(result.summary.total_bytes_read == bytes.size(),
             "file-backed replay should report the file byte size");
  ctx.Expect(result.summary.recognized_records == 10u &&
                 result.summary.runtime_updates == 7u,
             "file-backed replay should preserve the expected recognized-record and update counts");
  ctx.Expect(result.summary.counts_by_protocol.at("nmea") == 3u &&
                 result.summary.counts_by_protocol.at("ubx") == 3u &&
                 result.summary.counts_by_protocol.at("rtcm3") == 2u &&
                 result.summary.counts_by_protocol.at("unicore") == 2u,
             "file-backed replay should count protocols correctly");
  ctx.Expect(result.summary.counts_by_rtcm_message_type.at(1005u) == 1u &&
                 result.summary.counts_by_rtcm_message_type.at(1077u) == 1u,
             "file-backed replay should retain RTCM type counts");

  const auto& final_state = result.final_state;
  ctx.Expect(final_state.fix_type == universal_gnss::GnssFixType::kRtkFloat &&
                 final_state.rtk_mode == std::optional<universal_gnss::GnssRtkMode>(
                                        universal_gnss::GnssRtkMode::kFloat),
             "file-backed replay should end with the expected RTK float state");
  ctx.Expect(final_state.latitude_deg == std::optional<double>(40.0789588272) &&
                 final_state.longitude_deg == std::optional<double>(116.2365102982) &&
                 final_state.altitude_m == std::optional<double>(65.8312),
             "file-backed replay should preserve the final sanitized BESTNAVA position");
  ctx.Expect(final_state.satellites_used == std::optional<std::uint16_t>(28u) &&
                 final_state.satellites_tracked == std::optional<std::uint16_t>(3u) &&
                 final_state.satellites_visible == std::optional<std::uint16_t>(3u),
             "file-backed replay should merge satellite counts across protocols");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestReplayMergesMixedRuntimeState(ctx);
  TestReplaySummaryOnlyAndFormatting(ctx);
  TestReplayStreamInput(ctx);
  TestFileBackedReplay(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools replay tests passed\n";
  return EXIT_SUCCESS;
}
