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
  Append(bytes, BuildRtcmFrame(1005u));
  Append(bytes, BuildRtcmFrame(1077u));
  return bytes;
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
}

}  // namespace

int main()
{
  TestContext ctx;

  TestReportFromNmeaLog(ctx);
  TestReportFromMixedLog(ctx);
  TestReceiverSideRtcmDiagnosticsAndRtkFixedClassification(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools quality report tests passed\n";
  return EXIT_SUCCESS;
}
