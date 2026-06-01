#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_tools/gnss_quality_report.hpp"
#include "testdata_utils.hpp"

namespace
{

using universal_gnss_tools::GnssQualityLevel;

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

std::vector<std::uint8_t> MakeNavStatusFixedPayload()
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

std::vector<std::uint8_t> MakeRxmRtcmPayload(const std::uint8_t flags,
                                             const std::uint16_t subtype,
                                             const std::uint16_t ref_station_id,
                                             const std::uint16_t message_type)
{
  std::vector<std::uint8_t> payload(8u, 0u);
  payload[0u] = 0x02u;
  payload[1u] = flags;
  WriteLeU2(payload, 2u, subtype);
  WriteLeU2(payload, 4u, ref_station_id);
  WriteLeU2(payload, 6u, message_type);
  return payload;
}

std::vector<std::uint8_t> BuildSyntheticRtkFixedQualityStream()
{
  std::vector<std::uint8_t> bytes;
  Append(bytes, BuildUbxFrame(0x01u, 0x03u, MakeNavStatusFixedPayload()));
  Append(bytes, BuildUbxFrame(0x02u, 0x32u, MakeRxmRtcmPayload(0x04u, 0u, 42u, 1077u)));
  Append(bytes, BuildUbxFrame(0x02u, 0x32u, MakeRxmRtcmPayload(0x01u, 0u, 42u, 1005u)));
  Append(bytes, BuildRtcm1006Frame(42u, 1234567LL, -2345678LL, 3456789LL, 4321u));
  Append(bytes, BuildRtcmFrame(1077u));
  return bytes;
}

std::vector<std::uint8_t> BuildUnicoreLine(const std::string& line)
{
  return std::vector<std::uint8_t>(line.begin(), line.end());
}

void TestReportFromNmeaLog(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("nmea/basic_fix.nmea");
  const auto report = universal_gnss_tools::BuildGnssQualityReportBytes(bytes);

  ctx.Expect(report.summary.total_bytes_read == bytes.size(),
             "quality report should preserve the total byte count");
  ctx.Expect(report.summary.records_processed == 5u &&
                 report.summary.runtime_updates == 5u &&
                 report.summary.counts_by_protocol.at("nmea") == 5u,
             "quality report should summarize the NMEA file through replay");
  ctx.Expect(report.final_state.fix_valid &&
                 report.final_state.fix_type == universal_gnss::GnssFixType::kFix,
             "quality report should retain the final basic fix state");
  ctx.Expect(report.summary.quality_level == GnssQualityLevel::kGood,
             "GST-backed accuracy should classify the basic NMEA fix as good");
  ctx.Expect(report.summary.best_horizontal_accuracy_m == std::optional<float>(0.6f) &&
                 report.summary.latest_horizontal_accuracy_m == std::optional<float>(0.6f) &&
                 report.summary.latest_vertical_accuracy_m == std::optional<float>(1.1f),
             "quality report should preserve best/latest conservative accuracy from GST");
  ctx.Expect(report.summary.latest_hdop == std::optional<float>(1.0f) &&
                 report.summary.latest_vdop == std::optional<float>(1.5f),
             "quality report should preserve the latest DOP values");
  ctx.Expect(report.rtcm.total_frames == 0u &&
                 report.diagnostics.empty(),
             "plain NMEA reports should not invent RTCM activity or diagnostics");
}

void TestReportFromMixedLog(TestContext& ctx)
{
  const auto bytes =
      universal_gnss_tools::test::ReadBinaryFile("mixed/nmea_ubx_rtcm_unicore.bin");
  const auto report = universal_gnss_tools::BuildGnssQualityReportBytes(bytes);

  ctx.Expect(report.summary.total_bytes_read == bytes.size(),
             "mixed quality report should preserve the total byte count");
  ctx.Expect(report.summary.records_processed == 10u &&
                 report.summary.runtime_updates == 7u &&
                 report.summary.counts_by_protocol.at("nmea") == 3u &&
                 report.summary.counts_by_protocol.at("ubx") == 3u &&
                 report.summary.counts_by_protocol.at("unicore") == 2u &&
                 report.summary.counts_by_protocol.at("rtcm3") == 2u,
             "mixed quality report should preserve replay protocol counters");
  ctx.Expect(report.summary.quality_level == GnssQualityLevel::kRtkFloat,
             "mixed report should classify the final Unicore state as RTK float");
  ctx.Expect(report.final_state.fix_valid &&
                 report.final_state.rtk_mode ==
                     std::optional<universal_gnss::GnssRtkMode>(universal_gnss::GnssRtkMode::kFloat),
             "mixed report should preserve the final RTK float mode");
  ctx.Expect(report.rtcm.total_frames == 2u &&
                 report.rtcm.valid_frames == 2u &&
                 report.rtcm.invalid_frames == 0u &&
                 report.rtcm.message_type_counts.at(1005u) == 1u &&
                 report.rtcm.message_type_counts.at(1077u) == 1u,
             "mixed report should summarize RTCM activity");
  ctx.Expect(report.summary.warning_count > 0u &&
                 !report.diagnostics.empty(),
             "mixed report should surface parser warnings for invalid/truncated data");

  const std::string text = universal_gnss_tools::FormatGnssQualityReportText(report);
  const std::string json = universal_gnss_tools::FormatGnssQualityReportJson(report);
  ctx.Expect(text.find("quality level=rtk_float") != std::string::npos &&
                 text.find("rtcm_types 1005=1 1077=1") != std::string::npos,
             "text report should include quality level and RTCM type counts");
  ctx.Expect(json.find("\"quality_level\":\"rtk_float\"") != std::string::npos &&
                 json.find("\"message_type_counts\":{\"1005\":1,\"1077\":1}") != std::string::npos,
             "JSON report should include the expected RTK level and RTCM counters");
}

void TestReceiverSideRtcmDiagnosticsAndRtkFixedClassification(TestContext& ctx)
{
  const auto bytes = BuildSyntheticRtkFixedQualityStream();
  const auto report = universal_gnss_tools::BuildGnssQualityReportBytes(bytes);

  ctx.Expect(report.summary.quality_level == GnssQualityLevel::kRtkFixed,
             "receiver-side fixed RTK state should classify as rtk_fixed");
  ctx.Expect(report.rtcm.total_frames == 2u &&
                 report.rtcm.receiver_side.events_observed == 2u &&
                 report.rtcm.receiver_side.accepted_messages == 1u &&
                 report.rtcm.receiver_side.not_used_messages == 0u &&
                 report.rtcm.receiver_side.crc_failed_messages == 1u,
             "receiver-side RTCM diagnostics should distinguish accepted and CRC-failed messages");
  ctx.Expect(report.rtcm.last_base_station_arp.has_value() &&
                 report.rtcm.last_base_station_arp->message_type == 1006u &&
                 report.rtcm.last_base_station_arp->station_id == 42u &&
                 report.rtcm.last_base_station_arp->antenna_height_m.has_value(),
             "quality report should retain the last decoded base station ARP record");
  ctx.Expect(report.summary.warning_count == 1u && report.summary.error_count == 0u,
             "receiver-side CRC failure should surface as one warning and no errors");

  bool saw_crc_failed = false;
  for (const auto& event : report.diagnostics)
  {
    if (event.code == "ubx_rxm_rtcm.crc_failed")
    {
      saw_crc_failed = true;
    }
  }
  ctx.Expect(saw_crc_failed,
             "quality report should preserve the RXM-RTCM CRC-failed diagnostic event");

  const std::string text = universal_gnss_tools::FormatGnssQualityReportText(report, true);
  const std::string json = universal_gnss_tools::FormatGnssQualityReportJson(report, true);
  ctx.Expect(text.find("rtcm_base station_id=42") != std::string::npos &&
                 text.find("antenna_height_m=0.4321") != std::string::npos,
             "text quality report should include decoded base station ARP details");
  ctx.Expect(json.find("\"base_station_arp\":{\"message_type\":1006") != std::string::npos &&
                 json.find("\"station_id\":42") != std::string::npos,
             "JSON quality report should include the decoded base station ARP object");
}

void TestUnicoreRfDiagnostics(TestContext& ctx)
{
  std::vector<std::uint8_t> bytes;
  Append(bytes, BuildUnicoreLine(
                    "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
                    "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
                    "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
                    "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n"));
  Append(bytes, BuildUnicoreLine(
                    "#JAMSTATUSA,97,GPS,FINE,2190,365412000,0,0,18,14;SINGLE,120,2,0,0*e31418ea\r\n"));
  Append(bytes, BuildUnicoreLine(
                    "#HWSTATUSA,97,GPS,FINE,2221,111183000,0,0,18,15;66807,0.920,1.020,0.908,0,0.693,0.0,0x00,0,0x0377,0,0*9d7ce51d\r\n"));

  const auto report = universal_gnss_tools::BuildGnssQualityReportBytes(bytes);

  ctx.Expect(report.final_state.interference_detected == std::optional<bool>(true) &&
                 report.final_state.jamming_detected == std::optional<bool>(true),
             "quality report should preserve Unicore runtime jamming state");
  ctx.Expect(report.summary.warning_count >= 1u && report.summary.error_count >= 1u,
             "Unicore jamming and hardware diagnostics should surface warning and error counts");

  bool saw_strong_jam = false;
  bool saw_hw_warning = false;
  for (const auto& event : report.diagnostics)
  {
    if (event.code == "unicore_jam_status.strong")
    {
      saw_strong_jam = true;
    }
    if (event.code == "unicore_hw_status.clock_invalid")
    {
      saw_hw_warning = true;
    }
  }

  ctx.Expect(saw_strong_jam && saw_hw_warning,
             "quality report should preserve Unicore RF and hardware diagnostic events");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestReportFromNmeaLog(ctx);
  TestReportFromMixedLog(ctx);
  TestReceiverSideRtcmDiagnosticsAndRtkFixedClassification(ctx);
  TestUnicoreRfDiagnostics(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools quality report tests passed\n";
  return EXIT_SUCCESS;
}
