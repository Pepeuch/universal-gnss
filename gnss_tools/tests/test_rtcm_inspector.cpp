#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"
#include "universal_gnss_protocols/rtcm_records.hpp"
#include "universal_gnss_tools/rtcm_inspector.hpp"
#include "testdata_utils.hpp"

namespace
{

using universal_gnss_protocols::RtcmConstellation;
using universal_gnss_tools::RtcmInspectionResult;

struct TestContext
{
  int failures{0};

  void Expect(bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

std::vector<std::uint8_t> BuildRtcmFrame(const std::uint16_t message_type,
                                         const bool valid_crc = true)
{
  const std::vector<std::uint8_t> payload = {
      static_cast<std::uint8_t>((message_type >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((message_type & 0x0Fu) << 4u),
  };

  std::vector<std::uint8_t> bytes = {0xD3u, 0x00u,
                                     static_cast<std::uint8_t>(payload.size())};
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  if (!valid_crc)
  {
    crc ^= 0x1u;
  }

  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

std::vector<std::uint8_t> BuildRtcmFrameFromPayload(const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> bytes = {0xD3u, 0x00u, static_cast<std::uint8_t>(payload.size())};
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

void Append(std::vector<std::uint8_t>& destination, const std::vector<std::uint8_t>& source)
{
  destination.insert(destination.end(), source.begin(), source.end());
}

void AppendBit(std::vector<std::uint8_t>& payload, std::size_t& bit_offset, const bool bit)
{
  if ((bit_offset % 8u) == 0u)
  {
    payload.push_back(0u);
  }

  if (bit)
  {
    payload.back() |= static_cast<std::uint8_t>(1u << (7u - (bit_offset % 8u)));
  }
  ++bit_offset;
}

void AppendUnsignedBits(std::vector<std::uint8_t>& payload,
                        std::size_t& bit_offset,
                        const std::uint64_t value,
                        const std::size_t bit_count)
{
  for (std::size_t i = 0u; i < bit_count; ++i)
  {
    const std::size_t shift = bit_count - 1u - i;
    AppendBit(payload, bit_offset, ((value >> shift) & 0x01u) != 0u);
  }
}

void AppendSignedBits(std::vector<std::uint8_t>& payload,
                      std::size_t& bit_offset,
                      const std::int64_t value,
                      const std::size_t bit_count)
{
  const std::uint64_t mask = (1ULL << bit_count) - 1ULL;
  AppendUnsignedBits(payload, bit_offset, static_cast<std::uint64_t>(value) & mask, bit_count);
}

void AppendZeroBits(std::vector<std::uint8_t>& payload,
                    std::size_t& bit_offset,
                    const std::size_t bit_count)
{
  for (std::size_t index = 0u; index < bit_count; ++index)
  {
    AppendBit(payload, bit_offset, false);
  }
}

std::size_t GetRtcmMsmBodyBits(const std::uint8_t msm_variant,
                               const std::size_t satellite_count,
                               const std::size_t populated_cell_count)
{
  switch (msm_variant)
  {
    case 4u:
      return satellite_count * 18u + populated_cell_count * 48u;
    case 5u:
      return satellite_count * 36u + populated_cell_count * 63u;
    case 6u:
      return satellite_count * 18u + populated_cell_count * 65u;
    case 7u:
      return satellite_count * 36u + populated_cell_count * 80u;
    default:
      return 0u;
  }
}

std::vector<std::uint8_t> BuildRtcm1006Frame(const std::uint16_t station_id,
                                             const std::int64_t ecef_x_0_1mm,
                                             const std::int64_t ecef_y_0_1mm,
                                             const std::int64_t ecef_z_0_1mm,
                                             const std::uint16_t antenna_height_0_1mm)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;
  AppendUnsignedBits(payload, bit_offset, 1006u, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, 21u, 6u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendSignedBits(payload, bit_offset, ecef_x_0_1mm, 38u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendSignedBits(payload, bit_offset, ecef_y_0_1mm, 38u);
  AppendUnsignedBits(payload, bit_offset, 1u, 2u);
  AppendSignedBits(payload, bit_offset, ecef_z_0_1mm, 38u);
  AppendUnsignedBits(payload, bit_offset, antenna_height_0_1mm, 16u);

  std::vector<std::uint8_t> bytes = {0xD3u, 0x00u, static_cast<std::uint8_t>(payload.size())};
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

std::vector<std::uint8_t> BuildRtcm1230Frame(const std::uint16_t station_id,
                                             const bool code_phase_bias_indicator,
                                             const bool has_l1_ca_bias,
                                             const bool has_l1_p_bias,
                                             const bool has_l2_ca_bias,
                                             const bool has_l2_p_bias,
                                             const std::optional<std::int16_t> l1_ca_bias_raw,
                                             const std::optional<std::int16_t> l1_p_bias_raw,
                                             const std::optional<std::int16_t> l2_ca_bias_raw,
                                             const std::optional<std::int16_t> l2_p_bias_raw)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;
  AppendUnsignedBits(payload, bit_offset, 1230u, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, code_phase_bias_indicator ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 3u);
  AppendUnsignedBits(payload, bit_offset, has_l1_ca_bias ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, has_l1_p_bias ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, has_l2_ca_bias ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, has_l2_p_bias ? 1u : 0u, 1u);
  if (has_l1_ca_bias)
  {
    AppendSignedBits(payload, bit_offset, *l1_ca_bias_raw, 16u);
  }
  if (has_l1_p_bias)
  {
    AppendSignedBits(payload, bit_offset, *l1_p_bias_raw, 16u);
  }
  if (has_l2_ca_bias)
  {
    AppendSignedBits(payload, bit_offset, *l2_ca_bias_raw, 16u);
  }
  if (has_l2_p_bias)
  {
    AppendSignedBits(payload, bit_offset, *l2_p_bias_raw, 16u);
  }

  std::vector<std::uint8_t> bytes = {0xD3u, 0x00u, static_cast<std::uint8_t>(payload.size())};
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

std::vector<std::uint8_t> BuildRtcmMsmPayload(const std::uint16_t message_type,
                                              const std::uint16_t station_id,
                                              const std::vector<std::uint8_t>& satellite_ids,
                                              const std::vector<std::uint8_t>& signal_ids,
                                              const std::vector<bool>& cell_mask,
                                              const bool multiple_message = false,
                                              const std::uint8_t issue_of_data_station = 0u)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;

  AppendUnsignedBits(payload, bit_offset, message_type, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, 123456u, 30u);
  AppendUnsignedBits(payload, bit_offset, multiple_message ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, issue_of_data_station, 3u);
  AppendUnsignedBits(payload, bit_offset, 15u, 7u);
  AppendUnsignedBits(payload, bit_offset, 1u, 2u);
  AppendUnsignedBits(payload, bit_offset, 0u, 2u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 3u, 3u);

  for (std::uint8_t satellite = 1u; satellite <= 64u; ++satellite)
  {
    bool present = false;
    for (const auto candidate : satellite_ids)
    {
      if (candidate == satellite)
      {
        present = true;
        break;
      }
    }
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
  }

  for (std::uint8_t signal = 1u; signal <= 32u; ++signal)
  {
    bool present = false;
    for (const auto candidate : signal_ids)
    {
      if (candidate == signal)
      {
        present = true;
        break;
      }
    }
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
  }

  std::size_t populated_cell_count = 0u;
  for (const bool present : cell_mask)
  {
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
    if (present)
    {
      ++populated_cell_count;
    }
  }

  AppendZeroBits(payload,
                 bit_offset,
                 GetRtcmMsmBodyBits(universal_gnss_protocols::GetRtcmMsmVariant(message_type),
                                    satellite_ids.size(),
                                    populated_cell_count));
  return payload;
}

std::vector<std::uint8_t> BuildRtcmMsmFrame(const std::uint16_t message_type,
                                            const std::uint16_t station_id,
                                            const std::vector<std::uint8_t>& satellite_ids,
                                            const std::vector<std::uint8_t>& signal_ids,
                                            const std::vector<bool>& cell_mask,
                                            const bool multiple_message = false,
                                            const std::uint8_t issue_of_data_station = 0u)
{
  return BuildRtcmFrameFromPayload(BuildRtcmMsmPayload(message_type,
                                                       station_id,
                                                       satellite_ids,
                                                       signal_ids,
                                                       cell_mask,
                                                       multiple_message,
                                                       issue_of_data_station));
}

RtcmInspectionResult BuildSyntheticInspectionResult()
{
  std::vector<std::uint8_t> stream = {0x55u, 0xAAu, 0x01u};
  Append(stream, BuildRtcmFrame(1005u));
  Append(stream, BuildRtcmFrame(1077u));
  Append(stream, BuildRtcmFrame(1087u));

  const auto truncated = BuildRtcmFrame(1005u);
  stream.insert(stream.end(), truncated.begin(), truncated.begin() + 4);

  return universal_gnss_tools::InspectRtcmBytes(stream);
}

void TestInspectionCounts(TestContext& ctx)
{
  const auto result = BuildSyntheticInspectionResult();
  ctx.Expect(result.summary.total_bytes_read == 31u,
             "inspection should report the total bytes read");
  ctx.Expect(result.summary.total_frames_found == 3u,
             "inspection should find three complete frames");
  ctx.Expect(result.summary.valid_frames == 3u && result.summary.invalid_frames == 0u,
             "inspection should count valid and invalid frames");
  ctx.Expect(result.summary.malformed_events == 1u && result.summary.truncated_frames == 1u,
             "inspection should report the truncated trailing frame");
  ctx.Expect(result.frames.size() == 3u,
             "inspection should retain per-frame records by default");
  ctx.Expect(result.frames[0].byte_offset == 3u &&
                 result.frames[1].byte_offset == 11u &&
                 result.frames[2].byte_offset == 19u,
             "inspection should expose frame byte offsets");
  ctx.Expect(result.summary.counts_by_message_type.at(1005u) == 1u &&
                 result.summary.counts_by_message_type.at(1077u) == 1u &&
                 result.summary.counts_by_message_type.at(1087u) == 1u,
             "inspection should count frames by RTCM message type");
  ctx.Expect(result.summary.msm_counts_by_constellation.at(RtcmConstellation::kGps) == 1u &&
                 result.summary.msm_counts_by_constellation.at(RtcmConstellation::kGlonass) == 1u,
             "inspection should count MSM constellations");
}

void TestInvalidCrcFrameHandling(TestContext& ctx)
{
  const auto result =
      universal_gnss_tools::InspectRtcmBytes(BuildRtcmFrame(1230u, false), false);

  ctx.Expect(result.frames.empty(),
             "inspection should omit frame details when include_frames is false");
  ctx.Expect(result.summary.total_frames_found == 1u,
             "inspection should still count CRC-invalid frames");
  ctx.Expect(result.summary.valid_frames == 0u && result.summary.invalid_frames == 1u,
             "inspection should track CRC-invalid frames separately");
  ctx.Expect(result.summary.counts_by_message_type.at(1230u) == 1u,
             "inspection should still count the message type of a CRC-invalid frame");
}

void TestFormattedOutput(TestContext& ctx)
{
  const auto result = BuildSyntheticInspectionResult();
  const std::string text = universal_gnss_tools::FormatRtcmInspectionText(result, false);
  const std::string summary = universal_gnss_tools::FormatRtcmInspectionText(result, true);
  const std::string json = universal_gnss_tools::FormatRtcmInspectionJson(result, true);

  ctx.Expect(text.find("1 offset=3 len=8 type=1005 class=station_arp crc=valid") !=
                 std::string::npos,
             "text output should include the first frame summary");
  ctx.Expect(text.find("type=1077 class=msm:gps crc=valid") != std::string::npos,
             "text output should classify GPS MSM frames");
  ctx.Expect(summary.find("summary total_bytes=31 frames=3 valid=3 invalid=0 malformed=1 truncated=1") !=
                 std::string::npos,
             "summary output should include aggregate counts");
  ctx.Expect(summary.find("message_types 1005=1 1077=1 1087=1") != std::string::npos,
             "summary output should include counts by message type");
  ctx.Expect(summary.find("msm_constellations gps=1 glonass=1") != std::string::npos,
             "summary output should include MSM constellation counts");
  ctx.Expect(summary.find("base_station_arp seen=true decoded=false valid=false") !=
                 std::string::npos,
             "summary output should expose semantic RTCM observations even without a decodable ARP payload");
  ctx.Expect(json.find("\"total_frames_found\":3") != std::string::npos &&
                 json.find("\"1005\":1") != std::string::npos &&
                 json.find("\"gps\":1") != std::string::npos &&
                 json.find("\"semantic_observations\":[") != std::string::npos &&
                 json.find("\"base_station_arp\":null") != std::string::npos,
             "JSON summary should include the expected aggregate counters");
}

void TestDecodedBaseStationPositionSummary(TestContext& ctx)
{
  const auto bytes = BuildRtcm1006Frame(88u, 1234567LL, -2345678LL, 3456789LL, 4321u);
  const auto result = universal_gnss_tools::InspectRtcmBytes(bytes, false);
  const std::string summary = universal_gnss_tools::FormatRtcmInspectionText(result, true);
  const std::string json = universal_gnss_tools::FormatRtcmInspectionJson(result, true);

  ctx.Expect(result.summary.last_base_station_arp.has_value() &&
                 result.summary.last_base_station_arp->station_id == 88u,
             "inspection should decode and retain the latest base station ARP record");
  ctx.Expect(summary.find("base_station_arp seen=true decoded=true valid=true") !=
                     std::string::npos &&
                 summary.find("station_id=88") != std::string::npos &&
                 summary.find("antenna_height_m=0.4321") != std::string::npos,
             "summary output should expose decoded base station ARP details");
  ctx.Expect(json.find("\"base_station_arp\":{\"message_type\":1006") != std::string::npos &&
                 json.find("\"station_id\":88") != std::string::npos,
             "JSON summary should expose the decoded base station ARP object");
}

void TestDecodedGlonassBiasSummary(TestContext& ctx)
{
  const auto bytes = BuildRtcm1230Frame(42u,
                                        true,
                                        true,
                                        false,
                                        true,
                                        true,
                                        10,
                                        std::nullopt,
                                        -5,
                                        7);
  const auto result = universal_gnss_tools::InspectRtcmBytes(bytes, false);
  const std::string summary = universal_gnss_tools::FormatRtcmInspectionText(result, true);
  const std::string json = universal_gnss_tools::FormatRtcmInspectionJson(result, true);

  ctx.Expect(summary.find("glonass_code_phase_bias seen=true decoded=true valid=true") !=
                 std::string::npos &&
                 summary.find("station_id=42") != std::string::npos &&
                 summary.find("signal_mask=0xD") != std::string::npos &&
                 summary.find("l1_ca_bias_m=0.2000") != std::string::npos,
             "summary output should expose decoded RTCM 1230 semantic content");
  ctx.Expect(json.find("\"name\":\"glonass_code_phase_bias\"") != std::string::npos &&
                 json.find("\"signal_mask\":\"0xD\"") != std::string::npos &&
                 json.find("\"station_id\":\"42\"") != std::string::npos,
             "JSON summary should expose decoded RTCM 1230 semantic observations");
}

void TestDecodedMsmSummary(TestContext& ctx)
{
  std::vector<std::uint8_t> bytes;
  Append(bytes, BuildRtcmMsmFrame(1077u, 42u, {1u, 3u}, {1u, 5u}, {true, false, true, true}, true, 5u));
  Append(bytes, BuildRtcmMsmFrame(1087u, 42u, {2u}, {1u, 3u, 4u}, {true, false, true}));

  const auto result = universal_gnss_tools::InspectRtcmBytes(bytes, false);
  const std::string summary = universal_gnss_tools::FormatRtcmInspectionText(result, true);
  const std::string json = universal_gnss_tools::FormatRtcmInspectionJson(result, true);

  ctx.Expect(summary.find("msm_summary seen=true decoded=true valid=true decode_success=2 decode_failure=0 malformed=0 message_type=1087") !=
                     std::string::npos &&
                 summary.find("constellations_seen=gps,glonass") != std::string::npos &&
                 summary.find("station_id=42") != std::string::npos &&
                 summary.find("satellite_count=1") != std::string::npos &&
                 summary.find("signal_count=3") != std::string::npos &&
                 summary.find("cell_count=2") != std::string::npos,
             "summary output should expose the aggregate portable MSM summary");
  ctx.Expect(summary.find("msm_gps_msm7 seen=true decoded=true valid=true") != std::string::npos &&
                 summary.find("msm_glonass_msm7 seen=true decoded=true valid=true") !=
                     std::string::npos,
             "summary output should expose per-message MSM semantic observations");
  ctx.Expect(json.find("\"name\":\"msm_summary\"") != std::string::npos &&
                 json.find("\"constellations_seen\":\"gps,glonass\"") != std::string::npos &&
                 json.find("\"name\":\"msm_glonass_msm7\"") != std::string::npos &&
                 json.find("\"cell_count\":\"2\"") != std::string::npos,
             "JSON summary should expose aggregate and per-message MSM semantic observations");
}

void TestMalformedMsmSummary(TestContext& ctx)
{
  auto malformed_payload =
      BuildRtcmMsmPayload(1077u, 42u, {1u}, {1u}, {true}, false, 1u);
  malformed_payload.resize(21u);
  const auto bytes = BuildRtcmFrameFromPayload(malformed_payload);
  const auto result = universal_gnss_tools::InspectRtcmBytes(bytes, false);
  const std::string summary = universal_gnss_tools::FormatRtcmInspectionText(result, true);

  ctx.Expect(summary.find("msm_summary seen=true decoded=false valid=false decode_success=0 decode_failure=1 malformed=1 message_type=1077") !=
                 std::string::npos,
             "summary output should retain malformed MSM decode visibility");
}

void TestFileBackedInspection(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("rtcm/basic_msm.rtcm");
  const auto result = universal_gnss_tools::InspectRtcmBytes(bytes);

  ctx.Expect(result.summary.total_bytes_read == bytes.size(),
             "file-backed RTCM inspection should report the file byte size");
  ctx.Expect(result.summary.total_frames_found == 3u &&
                 result.summary.valid_frames == 3u &&
                 result.summary.invalid_frames == 0u,
             "file-backed RTCM inspection should find three valid frames");
  ctx.Expect(result.summary.malformed_events == 0u &&
                 result.summary.truncated_frames == 0u,
             "file-backed RTCM inspection should not report malformed trailing data");
  ctx.Expect(result.summary.counts_by_message_type.at(1005u) == 1u &&
                 result.summary.counts_by_message_type.at(1077u) == 1u &&
                 result.summary.counts_by_message_type.at(1087u) == 1u,
             "file-backed RTCM inspection should expose the expected message-type counts");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestInspectionCounts(ctx);
  TestInvalidCrcFrameHandling(ctx);
  TestFormattedOutput(ctx);
  TestDecodedBaseStationPositionSummary(ctx);
  TestDecodedGlonassBiasSummary(ctx);
  TestDecodedMsmSummary(ctx);
  TestMalformedMsmSummary(ctx);
  TestFileBackedInspection(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools RTCM inspector tests passed\n";
  return EXIT_SUCCESS;
}
