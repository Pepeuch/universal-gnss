#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_protocols/rtcm_crc24q.hpp"
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
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
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
  ctx.Expect(summary.find("base_station_arp unavailable") != std::string::npos,
             "summary output should report unavailable base station ARP when no full 1005/1006 payload is present");
  ctx.Expect(json.find("\"total_frames_found\":3") != std::string::npos &&
                 json.find("\"1005\":1") != std::string::npos &&
                 json.find("\"gps\":1") != std::string::npos &&
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
  ctx.Expect(summary.find("base_station_arp available station_id=88") != std::string::npos &&
                 summary.find("antenna_height_m=0.4321") != std::string::npos,
             "summary output should expose decoded base station ARP details");
  ctx.Expect(json.find("\"base_station_arp\":{\"message_type\":1006") != std::string::npos &&
                 json.find("\"station_id\":88") != std::string::npos,
             "JSON summary should expose the decoded base station ARP object");
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
  TestFileBackedInspection(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools RTCM inspector tests passed\n";
  return EXIT_SUCCESS;
}
