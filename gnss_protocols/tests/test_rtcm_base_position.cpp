#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"

namespace
{

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::RtcmCorrectionMonitor;
using universal_gnss_protocols::RtcmFrame;
using universal_gnss_protocols::RtcmFrameFramer;

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

  void ExpectNear(double lhs, double rhs, double epsilon, const std::string& message)
  {
    if (std::fabs(lhs - rhs) > epsilon)
    {
      ++failures;
      std::cerr << "FAILED: " << message << " lhs=" << lhs << " rhs=" << rhs << '\n';
    }
  }
};

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

std::vector<std::uint8_t> BuildBaseStationArpPayload(const std::uint16_t message_type,
                                                     const std::uint16_t station_id,
                                                     const std::uint8_t itrf_year,
                                                     const bool gps_indicator,
                                                     const bool glonass_indicator,
                                                     const bool galileo_indicator,
                                                     const bool reference_station_indicator,
                                                     const std::int64_t ecef_x_0_1mm,
                                                     const bool single_receiver_oscillator_indicator,
                                                     const std::int64_t ecef_y_0_1mm,
                                                     const std::uint8_t quarter_cycle_indicator,
                                                     const std::int64_t ecef_z_0_1mm,
                                                     const std::optional<std::uint16_t> antenna_height_0_1mm)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;

  AppendUnsignedBits(payload, bit_offset, message_type, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, itrf_year, 6u);
  AppendUnsignedBits(payload, bit_offset, gps_indicator ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, glonass_indicator ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, galileo_indicator ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, reference_station_indicator ? 1u : 0u, 1u);
  AppendSignedBits(payload, bit_offset, ecef_x_0_1mm, 38u);
  AppendUnsignedBits(
      payload, bit_offset, single_receiver_oscillator_indicator ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendSignedBits(payload, bit_offset, ecef_y_0_1mm, 38u);
  AppendUnsignedBits(payload, bit_offset, quarter_cycle_indicator, 2u);
  AppendSignedBits(payload, bit_offset, ecef_z_0_1mm, 38u);
  if (antenna_height_0_1mm.has_value())
  {
    AppendUnsignedBits(payload, bit_offset, *antenna_height_0_1mm, 16u);
  }

  return payload;
}

std::vector<std::uint8_t> BuildRtcmFrameBytes(const std::vector<std::uint8_t>& payload,
                                              const bool valid_crc = true)
{
  std::vector<std::uint8_t> bytes = {
      0xD3u,
      static_cast<std::uint8_t>((payload.size() >> 8u) & 0x03u),
      static_cast<std::uint8_t>(payload.size() & 0xFFu),
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

RtcmFrame ParseSingleFrame(const std::vector<std::uint8_t>& bytes)
{
  RtcmFrameFramer framer;
  for (const auto byte : bytes)
  {
    auto result = framer.PushByte(byte);
    if (result.status == ParserStatus::kRecordReady && result.record.has_value())
    {
      return *result.record;
    }
  }

  auto finalize = framer.Finalize();
  if (finalize.status == ParserStatus::kRecordReady && finalize.record.has_value())
  {
    return *finalize.record;
  }

  return RtcmFrame{};
}

RtcmFrame BuildBaseStationArpFrame(const std::uint16_t message_type,
                                   const std::uint16_t station_id,
                                   const std::uint8_t itrf_year,
                                   const bool gps_indicator,
                                   const bool glonass_indicator,
                                   const bool galileo_indicator,
                                   const bool reference_station_indicator,
                                   const std::int64_t ecef_x_0_1mm,
                                   const bool single_receiver_oscillator_indicator,
                                   const std::int64_t ecef_y_0_1mm,
                                   const std::uint8_t quarter_cycle_indicator,
                                   const std::int64_t ecef_z_0_1mm,
                                   const std::optional<std::uint16_t> antenna_height_0_1mm,
                                   const std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  RtcmFrame frame = ParseSingleFrame(BuildRtcmFrameBytes(BuildBaseStationArpPayload(
      message_type,
      station_id,
      itrf_year,
      gps_indicator,
      glonass_indicator,
      galileo_indicator,
      reference_station_indicator,
      ecef_x_0_1mm,
      single_receiver_oscillator_indicator,
      ecef_y_0_1mm,
      quarter_cycle_indicator,
      ecef_z_0_1mm,
      antenna_height_0_1mm)));
  frame.timestamp_ns = timestamp_ns;
  return frame;
}

void TestParseRtcm1005(TestContext& ctx)
{
  const RtcmFrame frame = BuildBaseStationArpFrame(
      1005u, 42u, 20u, true, true, false, true, 1234567LL, true, -2345678LL, 2u, 3456789LL,
      std::nullopt);

  const auto parsed = universal_gnss_protocols::ParseRtcmBaseStationArp(frame);
  ctx.Expect(parsed.status == ParserStatus::kRecordReady && parsed.record.has_value(),
             "valid RTCM 1005 should parse successfully");
  if (!parsed.record.has_value())
  {
    return;
  }

  ctx.Expect(parsed.record->message_type == 1005u, "1005 should preserve the message type");
  ctx.Expect(parsed.record->station_id == 42u, "1005 should decode the station id");
  ctx.Expect(parsed.record->itrf_year == 20u, "1005 should decode the ITRF year");
  ctx.Expect(parsed.record->gps_indicator && parsed.record->glonass_indicator &&
                 !parsed.record->galileo_indicator &&
                 parsed.record->reference_station_indicator,
             "1005 should decode constellation/reference indicators");
  ctx.ExpectNear(parsed.record->ecef_x_m, 123.4567, 1e-7,
                 "1005 should decode positive signed ECEF X");
  ctx.ExpectNear(parsed.record->ecef_y_m, -234.5678, 1e-7,
                 "1005 should decode negative signed ECEF Y");
  ctx.ExpectNear(parsed.record->ecef_z_m, 345.6789, 1e-7,
                 "1005 should decode positive signed ECEF Z");
  ctx.Expect(parsed.record->single_receiver_oscillator_indicator &&
                 parsed.record->quarter_cycle_indicator == 2u &&
                 !parsed.record->antenna_height_m.has_value(),
             "1005 should decode oscillator/quarter-cycle fields and omit antenna height");
}

void TestParseRtcm1006(TestContext& ctx)
{
  const RtcmFrame frame = BuildBaseStationArpFrame(
      1006u, 314u, 21u, true, false, true, false, -7654321LL, false, 1111111LL, 1u,
      -2222222LL, 12345u);

  const auto parsed = universal_gnss_protocols::ParseRtcmBaseStationArp(frame);
  ctx.Expect(parsed.status == ParserStatus::kRecordReady && parsed.record.has_value(),
             "valid RTCM 1006 should parse successfully");
  if (!parsed.record.has_value())
  {
    return;
  }

  ctx.Expect(parsed.record->message_type == 1006u, "1006 should preserve the message type");
  ctx.Expect(parsed.record->station_id == 314u, "1006 should decode the station id");
  ctx.ExpectNear(parsed.record->ecef_x_m, -765.4321, 1e-7,
                 "1006 should decode negative signed ECEF X");
  ctx.ExpectNear(parsed.record->ecef_y_m, 111.1111, 1e-7,
                 "1006 should decode positive signed ECEF Y");
  ctx.ExpectNear(parsed.record->ecef_z_m, -222.2222, 1e-7,
                 "1006 should decode negative signed ECEF Z");
  ctx.Expect(parsed.record->antenna_height_m.has_value(),
             "1006 should populate antenna height");
  if (parsed.record->antenna_height_m.has_value())
  {
    ctx.ExpectNear(*parsed.record->antenna_height_m,
                   1.2345,
                   1e-9,
                   "1006 should decode the antenna height");
  }
}

void TestRejectWrongMessageTypeAndTruncation(TestContext& ctx)
{
  const std::vector<std::uint8_t> wrong_type_payload = {
      static_cast<std::uint8_t>((1077u >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((1077u & 0x0Fu) << 4u),
  };
  RtcmFrame wrong_type = ParseSingleFrame(BuildRtcmFrameBytes(wrong_type_payload));
  wrong_type.checksum_status = ChecksumStatus::kValid;
  ctx.Expect(universal_gnss_protocols::ParseRtcmBaseStationArp(wrong_type).status ==
                 ParserStatus::kInvalidData,
             "non-1005/1006 frames should be rejected");

  RtcmFrame truncated = BuildBaseStationArpFrame(
      1005u, 1u, 1u, true, false, false, false, 1LL, false, 2LL, 0u, 3LL, std::nullopt);
  truncated.payload.pop_back();
  ctx.Expect(universal_gnss_protocols::ParseRtcmBaseStationArp(truncated).status ==
                 ParserStatus::kInvalidData,
             "truncated 1005 payloads should be rejected");
}

void TestCorrectionMonitorStoresLatestBasePosition(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveFrame(BuildBaseStationArpFrame(
      1005u, 7u, 18u, true, true, false, true, 100LL, false, 200LL, 0u, 300LL, std::nullopt,
      1000LL));
  monitor.ObserveFrame(BuildBaseStationArpFrame(
      1006u, 9u, 19u, true, false, true, false, -400LL, true, 500LL, 3u, -600LL, 70u, 2500LL));

  ctx.Expect(monitor.HasSeenBasePositionMessage() && monitor.HasSeenBasePosition1005() &&
                 monitor.HasSeenBasePosition1006(),
             "monitor should track 1005/1006 presence");
  ctx.Expect(monitor.HasBaseStationPosition(),
             "monitor should expose decoded base station position availability");
  ctx.Expect(monitor.LastBaseStationArpTimestampNs() == std::optional<std::int64_t>(2500LL),
             "monitor should retain the latest decoded base station timestamp");
  ctx.Expect(monitor.AgeSinceBaseStationArpNs(2600LL) == std::optional<std::int64_t>(100LL),
             "monitor should compute base-station ARP age when timestamps are available");
  ctx.Expect(monitor.last_base_station_arp().has_value() &&
                 monitor.last_base_station_arp()->station_id == 9u &&
                 monitor.last_base_station_arp()->antenna_height_m.has_value(),
             "monitor should retain the latest decoded base station ARP record");
  if (monitor.last_base_station_arp().has_value() &&
      monitor.last_base_station_arp()->antenna_height_m.has_value())
  {
    ctx.ExpectNear(*monitor.last_base_station_arp()->antenna_height_m,
                   0.007,
                   1e-9,
                   "monitor should preserve the decoded antenna height");
  }
}

}  // namespace

int main()
{
  TestContext ctx;

  TestParseRtcm1005(ctx);
  TestParseRtcm1006(ctx);
  TestRejectWrongMessageTypeAndTruncation(ctx);
  TestCorrectionMonitorStoresLatestBasePosition(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols RTCM base-position tests passed\n";
  return EXIT_SUCCESS;
}
