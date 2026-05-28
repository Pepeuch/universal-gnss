#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_tools/gnss_stream_inspector.hpp"
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

std::vector<std::uint8_t> BuildAsciiLine(const std::string& line)
{
  return std::vector<std::uint8_t>(line.begin(), line.end());
}

void Append(std::vector<std::uint8_t>& destination, const std::vector<std::uint8_t>& source)
{
  destination.insert(destination.end(), source.begin(), source.end());
}

struct SyntheticStream
{
  std::vector<std::uint8_t> bytes{};
  std::size_t gga_offset{0};
  std::size_t ubx_offset{0};
  std::size_t rtcm_offset{0};
  std::size_t invalid_ubx_offset{0};
};

SyntheticStream BuildSyntheticStream()
{
  const std::vector<std::uint8_t> noise = {0x00u, 0xFFu, 0x13u};
  const auto gga = BuildNmeaSentence(
      "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  const auto nav_pvt = BuildUbxFrame(0x01u, 0x07u, std::vector<std::uint8_t>(92u, 0u));
  const auto rtcm = BuildRtcmFrame(1005u);
  const auto invalid_nav_status =
      BuildUbxFrame(0x01u, 0x03u, std::vector<std::uint8_t>(16u, 0u), false);
  const auto truncated_rtcm = BuildRtcmFrame(1077u);

  SyntheticStream stream;
  stream.bytes = noise;
  stream.gga_offset = stream.bytes.size();
  Append(stream.bytes, gga);
  stream.ubx_offset = stream.bytes.size();
  Append(stream.bytes, nav_pvt);
  stream.rtcm_offset = stream.bytes.size();
  Append(stream.bytes, rtcm);
  stream.invalid_ubx_offset = stream.bytes.size();
  Append(stream.bytes, invalid_nav_status);
  stream.bytes.insert(stream.bytes.end(), truncated_rtcm.begin(), truncated_rtcm.begin() + 4);
  return stream;
}

void TestMixedStreamInspection(TestContext& ctx)
{
  const SyntheticStream stream = BuildSyntheticStream();
  const auto result = universal_gnss_tools::InspectGnssStreamBytes(stream.bytes);

  ctx.Expect(result.summary.total_bytes_read == stream.bytes.size(),
             "inspection should report the total bytes read");
  ctx.Expect(result.summary.total_items_found == 4u,
             "inspection should find four recognized mixed-protocol items");
  ctx.Expect(result.summary.valid_items == 3u && result.summary.invalid_items == 1u,
             "inspection should separate checksum-valid and checksum-invalid items");
  ctx.Expect(result.summary.malformed_events == 1u && result.summary.truncated_items == 1u,
             "inspection should report the truncated trailing frame");
  ctx.Expect(result.summary.noise_bytes == 3u && result.summary.noise_spans == 1u,
             "inspection should report the leading noise span");

  ctx.Expect(result.items.size() == 4u,
             "inspection should retain per-item records by default");
  ctx.Expect(result.items[0].byte_offset == stream.gga_offset &&
                 result.items[1].byte_offset == stream.ubx_offset &&
                 result.items[2].byte_offset == stream.rtcm_offset &&
                 result.items[3].byte_offset == stream.invalid_ubx_offset,
             "inspection should expose recognized item byte offsets");

  ctx.Expect(result.items[0].identity == "GPGGA" &&
                 result.items[0].protocol == universal_gnss_protocols::ProtocolType::kNmea,
             "inspection should identify the NMEA sentence");
  ctx.Expect(result.items[1].identity == "01:07" &&
                 result.items[1].ubx_message_name == "NAV-PVT",
             "inspection should identify the UBX NAV-PVT frame");
  ctx.Expect(result.items[2].rtcm_message_type == 1005u &&
                 result.items[2].classification == "station_arp",
             "inspection should classify the RTCM frame");
  ctx.Expect(result.items[3].ubx_message_name == "NAV-STATUS" &&
                 result.items[3].checksum_status == universal_gnss_protocols::ChecksumStatus::kInvalid,
             "inspection should keep checksum-invalid UBX frames as recognized items");

  ctx.Expect(result.summary.counts_by_protocol.at("nmea") == 1u &&
                 result.summary.counts_by_protocol.at("ubx") == 2u &&
                 result.summary.counts_by_protocol.at("rtcm3") == 1u,
             "inspection should count recognized items by protocol");
  ctx.Expect(result.summary.counts_by_nmea_sentence_type.at("GGA") == 1u,
             "inspection should count NMEA sentence types");
  ctx.Expect(result.summary.counts_by_ubx_message.at("01:07") == 1u &&
                 result.summary.counts_by_ubx_message.at("01:03") == 1u,
             "inspection should count UBX frames by class and id");
  ctx.Expect(result.summary.counts_by_rtcm_message_type.at(1005u) == 1u,
             "inspection should count RTCM frames by message type");
}

void TestSummaryOnlyAndFormatting(TestContext& ctx)
{
  const SyntheticStream stream = BuildSyntheticStream();
  const auto result = universal_gnss_tools::InspectGnssStreamBytes(stream.bytes, false);

  ctx.Expect(result.items.empty(),
             "inspection should omit per-item records when include_items is false");

  const auto full_result = universal_gnss_tools::InspectGnssStreamBytes(stream.bytes);
  const std::string text = universal_gnss_tools::FormatGnssStreamInspectionText(full_result);
  const std::string summary = universal_gnss_tools::FormatGnssStreamInspectionText(full_result, true);
  const std::string json = universal_gnss_tools::FormatGnssStreamInspectionJson(full_result);

  ctx.Expect(text.find("proto=nmea") != std::string::npos &&
                 text.find("id=GPGGA") != std::string::npos,
             "text output should include the NMEA timeline entry");
  ctx.Expect(text.find("proto=ubx") != std::string::npos &&
                 text.find("name=NAV-PVT") != std::string::npos &&
                 text.find("crc=invalid") != std::string::npos,
             "text output should include UBX names and invalid checksum status");
  ctx.Expect(text.find("proto=rtcm3") != std::string::npos &&
                 text.find("type=1005 class=station_arp") != std::string::npos,
             "text output should include RTCM classifications");
  ctx.Expect(summary.find("summary total_bytes=") != std::string::npos &&
                 summary.find("items=4 valid=3 invalid=1 malformed=1 truncated=1 noise_bytes=3 noise_spans=1") !=
                     std::string::npos,
             "summary output should include the expected aggregate counters");
  ctx.Expect(summary.find("protocols nmea=1 rtcm3=1 ubx=2") != std::string::npos &&
                 summary.find("nmea_types GGA=1") != std::string::npos &&
                 summary.find("ubx_messages 01:03=1 01:07=1") != std::string::npos &&
                 summary.find("rtcm_types 1005=1") != std::string::npos,
             "summary output should include protocol and message counters");
  ctx.Expect(json.find("\"protocol\":\"nmea\"") != std::string::npos &&
                 json.find("\"ubx_name\":\"NAV-PVT\"") != std::string::npos &&
                 json.find("\"message_type\":1005") != std::string::npos &&
                 json.find("\"counts_by_protocol\":{\"nmea\":1,\"rtcm3\":1,\"ubx\":2}") !=
                     std::string::npos,
             "JSON output should include items and compact aggregate maps");
}

void TestStreamInput(TestContext& ctx)
{
  const SyntheticStream stream = BuildSyntheticStream();
  std::string input_bytes(stream.bytes.begin(), stream.bytes.end());
  std::istringstream input(input_bytes);

  const auto result = universal_gnss_tools::InspectGnssStreamStream(input);
  ctx.Expect(result.summary.total_items_found == 4u &&
                 result.summary.truncated_items == 1u,
             "stream inspection should match byte-vector inspection");
}

void TestUnicoreInspection(TestContext& ctx)
{
  std::vector<std::uint8_t> bytes = {0x99u};
  Append(bytes,
         BuildAsciiLine(
             "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
             "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
             "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
             "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n"));

  const auto result = universal_gnss_tools::InspectGnssStreamBytes(bytes);
  ctx.Expect(result.summary.total_items_found == 1u &&
                 result.summary.counts_by_protocol.at("unicore") == 1u &&
                 result.summary.counts_by_unicore_message.at("BESTNAVA") == 1u,
             "inspection should recognize Unicore ASCII messages");
  ctx.Expect(result.items.size() == 1u &&
                 result.items[0].protocol == universal_gnss_protocols::ProtocolType::kUnicore &&
                 result.items[0].identity == "BESTNAVA",
             "inspection should expose the Unicore message identity");

  const std::string text = universal_gnss_tools::FormatGnssStreamInspectionText(result);
  const std::string json = universal_gnss_tools::FormatGnssStreamInspectionJson(result);
  ctx.Expect(text.find("proto=unicore") != std::string::npos &&
                 text.find("name=BESTNAVA") != std::string::npos &&
                 text.find("unicore_messages BESTNAVA=1") != std::string::npos,
             "text output should include Unicore timeline and counts");
  ctx.Expect(json.find("\"protocol\":\"unicore\"") != std::string::npos &&
                 json.find("\"unicore_message\":\"BESTNAVA\"") != std::string::npos &&
                 json.find("\"counts_by_unicore_message\":{\"BESTNAVA\":1}") != std::string::npos,
             "JSON output should include Unicore item and counters");
}

void TestFileBackedMixedInspection(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile(
      "mixed/nmea_ubx_rtcm_unicore.bin");
  const auto result = universal_gnss_tools::InspectGnssStreamBytes(bytes);

  ctx.Expect(result.summary.total_bytes_read == bytes.size(),
             "file-backed mixed inspection should report the file byte size");
  ctx.Expect(result.summary.total_items_found == 10u &&
                 result.summary.valid_items == 9u &&
                 result.summary.invalid_items == 1u,
             "file-backed mixed inspection should recognize the expected records");
  ctx.Expect(result.summary.malformed_events == 1u &&
                 result.summary.truncated_items == 1u &&
                 result.summary.noise_bytes == 3u &&
                 result.summary.noise_spans == 1u,
             "file-backed mixed inspection should retain malformed and noise statistics");
  ctx.Expect(result.summary.counts_by_protocol.at("nmea") == 3u &&
                 result.summary.counts_by_protocol.at("ubx") == 3u &&
                 result.summary.counts_by_protocol.at("rtcm3") == 2u &&
                 result.summary.counts_by_protocol.at("unicore") == 2u,
             "file-backed mixed inspection should count protocols correctly");
  ctx.Expect(result.summary.counts_by_unicore_message.at("BESTNAVA") == 1u &&
                 result.summary.counts_by_unicore_message.at("SATSINFOA") == 1u &&
                 result.summary.counts_by_rtcm_message_type.at(1005u) == 1u &&
                 result.summary.counts_by_rtcm_message_type.at(1077u) == 1u,
             "file-backed mixed inspection should classify Unicore and RTCM records");
  ctx.Expect(result.items.size() == 10u &&
                 result.items.front().identity == "GPGGA" &&
                 result.items.back().identity == "01:03",
             "file-backed mixed inspection should keep the expected timeline identities");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestMixedStreamInspection(ctx);
  TestSummaryOnlyAndFormatting(ctx);
  TestStreamInput(ctx);
  TestUnicoreInspection(ctx);
  TestFileBackedMixedInspection(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools mixed GNSS stream inspector tests passed\n";
  return EXIT_SUCCESS;
}
