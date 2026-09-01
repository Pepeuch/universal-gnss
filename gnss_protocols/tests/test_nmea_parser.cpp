#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_runtime_aggregator.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/nmea_parser.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::GnssRuntimeAggregator;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaDate;
using universal_gnss_protocols::NmeaFixDimension;
using universal_gnss_protocols::NmeaGgaFixQuality;
using universal_gnss_protocols::NmeaGsaMode;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::NmeaSentenceFramer;
using universal_gnss_protocols::NmeaUtcTime;
using universal_gnss_protocols::ParserStatus;

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

std::string MakeSentence(std::string payload_text)
{
  const std::uint8_t checksum = universal_gnss_protocols::ComputeNmeaChecksum(payload_text);

  std::ostringstream stream;
  stream << '$' << payload_text << '*'
         << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
         << static_cast<unsigned int>(checksum) << "\r\n";
  return stream.str();
}

NmeaSentence FrameSentence(const std::string& frame_text,
                           std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  NmeaSentenceFramer framer;
  universal_gnss_protocols::ParserResult<NmeaSentence> result;
  for (const char ch : frame_text)
  {
    result = framer.PushByte(static_cast<std::uint8_t>(ch), timestamp_ns);
  }

  if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
  {
    std::cerr << "FAILED: test setup could not frame NMEA sentence\n";
    std::exit(EXIT_FAILURE);
  }

  return *result.record;
}

bool NearlyEqual(double lhs, double rhs, double tolerance = 1e-5)
{
  return std::fabs(lhs - rhs) <= tolerance;
}

void ExpectUtcTime(TestContext& ctx,
                   const std::optional<NmeaUtcTime>& value,
                   std::uint8_t hour,
                   std::uint8_t minute,
                   double second,
                   const std::string& label)
{
  ctx.Expect(value.has_value(), label + " should be present");
  if (!value.has_value())
  {
    return;
  }

  ctx.Expect(value->hour == hour, label + " should preserve the hour");
  ctx.Expect(value->minute == minute, label + " should preserve the minute");
  ctx.Expect(NearlyEqual(value->second, second), label + " should preserve the seconds");
}

void ExpectDate(TestContext& ctx,
                const std::optional<NmeaDate>& value,
                std::uint8_t day,
                std::uint8_t month,
                std::uint8_t year_two_digits,
                const std::string& label)
{
  ctx.Expect(value.has_value(), label + " should be present");
  if (!value.has_value())
  {
    return;
  }

  ctx.Expect(value->day == day, label + " should preserve the day");
  ctx.Expect(value->month == month, label + " should preserve the month");
  ctx.Expect(value->year_two_digits == year_two_digits,
             label + " should preserve the two-digit year");
}

void TestCoordinateConversionHelpers(TestContext& ctx)
{
  const auto latitude = universal_gnss_protocols::ParseNmeaLatitude("4807.038", "N");
  const auto longitude = universal_gnss_protocols::ParseNmeaLongitude("01131.000", "E");
  const auto south = universal_gnss_protocols::ParseNmeaLatitude("4916.45", "S");
  const auto west = universal_gnss_protocols::ParseNmeaLongitude("12311.12", "W");

  ctx.Expect(latitude.has_value() && NearlyEqual(*latitude, 48.1173),
             "latitude conversion should match the known decimal degree example");
  ctx.Expect(longitude.has_value() && NearlyEqual(*longitude, 11.5166667),
             "longitude conversion should match the known decimal degree example");
  ctx.Expect(south.has_value() && NearlyEqual(*south, -49.2741667),
             "southern latitude should be negative");
  ctx.Expect(west.has_value() && NearlyEqual(*west, -123.1853333),
             "western longitude should be negative");
  ctx.Expect(!universal_gnss_protocols::ParseNmeaLatitude("4860.000", "N").has_value(),
             "invalid latitude minutes should be rejected");
}

void TestValidGgaParsing(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),
      987654321);
  const auto result = universal_gnss_protocols::ParseNmeaGga(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid GGA should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(987654321),
             "GGA should preserve the framing timestamp");
  ExpectUtcTime(ctx, record.utc_time, 12u, 35u, 19.0, "GGA utc time");
  ctx.Expect(record.fix_quality == NmeaGgaFixQuality::kGpsFix,
             "GGA should decode the fix quality");
  ctx.Expect(record.fix_valid, "GGA fix quality 1 should be valid");
  ctx.Expect(record.latitude_deg.has_value() && NearlyEqual(*record.latitude_deg, 48.1173),
             "GGA should decode latitude");
  ctx.Expect(record.longitude_deg.has_value() && NearlyEqual(*record.longitude_deg, 11.5166667),
             "GGA should decode longitude");
  ctx.Expect(record.altitude_m.has_value() && NearlyEqual(*record.altitude_m, 545.4),
             "GGA should decode altitude");
  ctx.Expect(record.satellites_used == std::optional<std::uint16_t>(8u),
             "GGA should decode satellites used");
  ctx.Expect(record.hdop.has_value() && NearlyEqual(*record.hdop, 0.9),
             "GGA should decode HDOP");
}

void TestValidRmcParsing(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"));
  const auto result = universal_gnss_protocols::ParseNmeaRmc(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid RMC should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ExpectUtcTime(ctx, record.utc_time, 12u, 35u, 19.0, "RMC utc time");
  ExpectDate(ctx, record.date, 23u, 3u, 94u, "RMC date");
  ctx.Expect(record.fix_valid, "RMC status A should be valid");
  ctx.Expect(record.latitude_deg.has_value() && NearlyEqual(*record.latitude_deg, 48.1173),
             "RMC should decode latitude");
  ctx.Expect(record.longitude_deg.has_value() && NearlyEqual(*record.longitude_deg, 11.5166667),
             "RMC should decode longitude");
  ctx.Expect(record.speed_over_ground_knots.has_value() &&
                 NearlyEqual(*record.speed_over_ground_knots, 22.4),
             "RMC should decode speed over ground in knots");
  ctx.Expect(record.course_over_ground_deg.has_value() &&
                 NearlyEqual(*record.course_over_ground_deg, 84.4),
             "RMC should decode course over ground");
}

void TestMalformedChecksumRejected(TestContext& ctx)
{
  const NmeaSentence sentence =
      FrameSentence("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00\r\n");
  ctx.Expect(sentence.checksum_status == ChecksumStatus::kInvalid,
             "test setup should produce an invalid checksum");

  ctx.Expect(universal_gnss_protocols::ParseNmeaGga(sentence).status == ParserStatus::kInvalidData,
             "GGA semantic parsing should reject invalid checksums");
}

void TestMalformedCoordinatesRejected(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPRMC,123519,A,4860.000,N,01131.000,E,022.4,084.4,230394,003.1,W"));
  ctx.Expect(universal_gnss_protocols::ParseNmeaRmc(sentence).status == ParserStatus::kInvalidData,
             "RMC should reject malformed coordinates");
}

void TestMissingOptionalFieldsAreTolerated(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGGA,123520,,,,,0,00,,,,,,"));
  const auto result = universal_gnss_protocols::ParseNmeaGga(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "GGA should tolerate missing optional fields");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(!record.fix_valid, "fix quality 0 should mark the record invalid");
  ctx.Expect(!record.latitude_deg.has_value() && !record.longitude_deg.has_value(),
             "missing coordinates should stay unset");
  ctx.Expect(!record.altitude_m.has_value(), "missing altitude should stay unset");
  ctx.Expect(record.satellites_used == std::optional<std::uint16_t>(0u),
             "satellites used should still parse when present");
  ctx.Expect(!record.hdop.has_value(), "missing HDOP should stay unset");
}

void TestSouthWestCoordinates(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPRMC,225446,A,4916.45,S,12311.12,W,000.5,054.7,191194,020.3,E"));
  const auto result = universal_gnss_protocols::ParseNmeaRmc(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "RMC south/west coordinates should parse");
  if (!result.record.has_value())
  {
    return;
  }

  ctx.Expect(result.record->latitude_deg.has_value() &&
                 NearlyEqual(*result.record->latitude_deg, -49.2741667),
             "southern RMC latitude should be negative");
  ctx.Expect(result.record->longitude_deg.has_value() &&
                 NearlyEqual(*result.record->longitude_deg, -123.1853333),
             "western RMC longitude should be negative");
}

void TestInvalidFixQualityRejected(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGGA,123519,4807.038,N,01131.000,E,9,08,0.9,545.4,M,46.9,M,,"));
  ctx.Expect(universal_gnss_protocols::ParseNmeaGga(sentence).status == ParserStatus::kInvalidData,
             "unsupported GGA fix quality values should be rejected");
}

void TestRuntimeMappingBehavior(TestContext& ctx)
{
  const NmeaSentence gga_sentence = FrameSentence(
      MakeSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),
      111);
  const auto gga_result = universal_gnss_protocols::ParseNmeaGga(gga_sentence);
  ctx.Expect(gga_result.record.has_value(), "runtime mapping test requires a parsed GGA record");
  if (!gga_result.record.has_value())
  {
    return;
  }

  const auto gga_state = universal_gnss_protocols::NmeaGgaToRuntimeState(*gga_result.record);
  ctx.Expect(gga_state.timestamp_ns == std::optional<std::int64_t>(111),
             "GGA runtime mapping should preserve the sample timestamp");
  ctx.Expect(gga_state.fix_valid && gga_state.fix_type == GnssFixType::kFix,
             "GGA runtime mapping should conservatively produce a generic fix");
  ctx.Expect(universal_gnss::HasCapability(gga_state, GnssCapability::kHdop),
             "GGA runtime mapping should advertise HDOP capability");
  ctx.Expect(universal_gnss::HasCapability(gga_state, GnssCapability::kSatellitesUsed),
             "GGA runtime mapping should advertise satellites-used capability");
  ctx.Expect(universal_gnss::HasValueAvailable(gga_state, GnssCapability::kHdop),
             "GGA runtime mapping should expose HDOP when present");
  ctx.Expect(universal_gnss::HasValueAvailable(gga_state, GnssCapability::kSatellitesUsed),
             "GGA runtime mapping should expose satellites used when present");
  ctx.Expect(universal_gnss::HasCapability(gga_state, GnssCapability::kRtkMode),
             "GGA runtime mapping should advertise RTK capability when standard fix quality is present");
  ctx.Expect(universal_gnss::HasValueAvailable(gga_state, GnssCapability::kRtkMode),
             "GGA runtime mapping should expose RTK mode when fix quality is known");
  ctx.Expect(gga_state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kNone),
             "GGA fix quality 1 should map to a known non-RTK runtime mode");
  ctx.Expect(!gga_state.heading_deg.has_value(),
             "GGA runtime mapping should not invent heading");

  const auto expect_gga_rtk_mode = [&](const std::string& payload,
                                       const GnssRtkMode expected_rtk_mode,
                                       const bool expected_fix_valid,
                                       const GnssFixType expected_fix_type,
                                       const std::string& label)
  {
    const auto sentence = FrameSentence(MakeSentence(payload), 150);
    const auto result = universal_gnss_protocols::ParseNmeaGga(sentence);
    ctx.Expect(result.record.has_value(), label + " should parse");
    if (!result.record.has_value())
    {
      return;
    }

    const auto state = universal_gnss_protocols::NmeaGgaToRuntimeState(*result.record);
    ctx.Expect(state.fix_valid == expected_fix_valid, label + " should preserve fix validity");
    ctx.Expect(state.fix_type == expected_fix_type, label + " should preserve generic fix type");
    ctx.Expect(universal_gnss::HasCapability(state, GnssCapability::kRtkMode),
               label + " should advertise RTK capability");
    ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kRtkMode),
               label + " should expose a known RTK mode");
    ctx.Expect(state.rtk_mode == std::optional<GnssRtkMode>(expected_rtk_mode),
               label + " should map to the expected RTK mode");
  };

  expect_gga_rtk_mode("GPGGA,123520,4807.038,N,01131.000,E,2,08,0.9,545.4,M,46.9,M,,",
                      GnssRtkMode::kNone,
                      true,
                      GnssFixType::kFix,
                      "GGA fix quality 2");
  expect_gga_rtk_mode("GPGGA,123521,4807.038,N,01131.000,E,4,08,0.9,545.4,M,46.9,M,,",
                      GnssRtkMode::kFixed,
                      true,
                      GnssFixType::kFix,
                      "GGA fix quality 4");
  expect_gga_rtk_mode("GPGGA,123522,4807.038,N,01131.000,E,5,08,0.9,545.4,M,46.9,M,,",
                      GnssRtkMode::kFloat,
                      true,
                      GnssFixType::kFix,
                      "GGA fix quality 5");
  expect_gga_rtk_mode(
      "GPGGA,123523,,,,,0,00,,,,,,", GnssRtkMode::kNone, false, GnssFixType::kNoFix, "GGA fix quality 0");

  const NmeaSentence rmc_sentence = FrameSentence(
      MakeSentence("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"),
      222);
  const auto rmc_result = universal_gnss_protocols::ParseNmeaRmc(rmc_sentence);
  ctx.Expect(rmc_result.record.has_value(), "runtime mapping test requires a parsed RMC record");
  if (!rmc_result.record.has_value())
  {
    return;
  }

  const auto rmc_state = universal_gnss_protocols::NmeaRmcToRuntimeState(*rmc_result.record);
  ctx.Expect(rmc_state.timestamp_ns == std::optional<std::int64_t>(222),
             "RMC runtime mapping should preserve the sample timestamp");
  ctx.Expect(rmc_state.fix_valid && rmc_state.fix_type == GnssFixType::kFix,
             "RMC runtime mapping should conservatively produce a generic fix");
  ctx.Expect(universal_gnss::HasValueAvailable(rmc_state, GnssCapability::kSpeedOverGround) &&
                 universal_gnss::HasValueAvailable(rmc_state, GnssCapability::kCourseOverGround) &&
                 NearlyEqual(*rmc_state.speed_over_ground_m_s, 22.4 * 0.514444) &&
                 rmc_state.course_over_ground_deg == std::optional<float>(84.4f),
             "RMC runtime mapping should expose independent ground speed and course flags");
  ctx.Expect(!rmc_state.altitude_m.has_value(),
             "RMC runtime mapping should not invent altitude");
  ctx.Expect(!rmc_state.heading_deg.has_value(),
             "RMC runtime mapping should not map course over ground to heading");
}

void TestValidGsaParsing(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5"),
      333);
  const auto result = universal_gnss_protocols::ParseNmeaGsa(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid GSA should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(333),
             "GSA should preserve the framing timestamp");
  ctx.Expect(record.fix_mode == NmeaGsaMode::kAutomatic,
             "GSA should decode the automatic/manual mode");
  ctx.Expect(record.fix_dimension == NmeaFixDimension::k3D,
             "GSA should decode the fix dimension");
  ctx.Expect(record.pdop.has_value() && NearlyEqual(*record.pdop, 1.8),
             "GSA should decode PDOP");
  ctx.Expect(record.hdop.has_value() && NearlyEqual(*record.hdop, 1.0),
             "GSA should decode HDOP");
  ctx.Expect(record.vdop.has_value() && NearlyEqual(*record.vdop, 1.5),
             "GSA should decode VDOP");
  ctx.Expect(record.active_satellite_count == 8u,
             "GSA should count active satellite PRNs");
  ctx.Expect(record.active_satellite_prns[0] == std::optional<std::uint16_t>(4u) &&
                 record.active_satellite_prns[7] == std::optional<std::uint16_t>(31u),
             "GSA should preserve active satellite PRNs");
}

void TestValidGsvParsing(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"),
      444);
  const auto result = universal_gnss_protocols::ParseNmeaGsv(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid GSV should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(444),
             "GSV should preserve the framing timestamp");
  ctx.Expect(record.total_messages == 2u && record.message_index == 1u,
             "GSV should decode message sequencing");
  ctx.Expect(record.satellites_in_view == 8u,
             "GSV should decode the visible satellite count");
  ctx.Expect(record.satellite_count == 4u,
             "GSV should decode all complete satellite blocks");
  ctx.Expect(record.satellites[0].prn == std::optional<std::uint16_t>(1u) &&
                 record.satellites[0].elevation_deg == std::optional<std::uint8_t>(40u) &&
                 record.satellites[0].azimuth_deg == std::optional<std::uint16_t>(83u) &&
                 record.satellites[0].cn0_db_hz.has_value() &&
                 NearlyEqual(*record.satellites[0].cn0_db_hz, 41.0),
             "GSV should decode per-satellite fields");
}

void TestPartialGsvSatelliteBlockHandling(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGSV,2,2,08,15,05,300,37,18,30,045,40,20,15"));
  const auto result = universal_gnss_protocols::ParseNmeaGsv(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "GSV should tolerate a trailing incomplete satellite block");
  if (!result.record.has_value())
  {
    return;
  }

  ctx.Expect(result.record->satellite_count == 2u,
             "GSV should keep only complete satellite blocks");
  ctx.Expect(result.record->satellites[1].prn == std::optional<std::uint16_t>(18u),
             "GSV should preserve complete satellites before the truncated tail");
}

void TestMalformedDopRejected(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,X,1.0,1.5"));
  ctx.Expect(universal_gnss_protocols::ParseNmeaGsa(sentence).status == ParserStatus::kInvalidData,
             "GSA should reject malformed DOP values");
}

void TestMissingOptionalGsaFields(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGSA,M,1,,,,,,,,,,,,,,,"));
  const auto result = universal_gnss_protocols::ParseNmeaGsa(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "GSA should tolerate missing optional fields");
  if (!result.record.has_value())
  {
    return;
  }

  ctx.Expect(result.record->fix_mode == NmeaGsaMode::kManual,
             "GSA should still decode required fix mode");
  ctx.Expect(result.record->fix_dimension == NmeaFixDimension::kNoFix,
             "GSA should still decode required fix dimension");
  ctx.Expect(result.record->active_satellite_count == 0u,
             "GSA should keep active satellite count at zero when none are listed");
  ctx.Expect(!result.record->pdop.has_value() && !result.record->hdop.has_value() &&
                 !result.record->vdop.has_value(),
             "missing GSA DOP values should stay unset");
}

void TestGsvMissingOptionalFields(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGSV,1,1,02,01,40,083,,02,17,308,43"));
  const auto result = universal_gnss_protocols::ParseNmeaGsv(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "GSV should tolerate missing optional CN0 values");
  if (!result.record.has_value())
  {
    return;
  }

  ctx.Expect(result.record->satellite_count == 2u,
             "GSV should decode satellite blocks with partial optional data");
  ctx.Expect(!result.record->satellites[0].cn0_db_hz.has_value(),
             "missing CN0 should stay unset");
  ctx.Expect(result.record->satellites[1].cn0_db_hz.has_value() &&
                 NearlyEqual(*result.record->satellites[1].cn0_db_hz, 43.0),
             "present CN0 should still parse");
}

void TestValidGstParsing(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,0.6,1.1"),
      777);
  const auto result = universal_gnss_protocols::ParseNmeaGst(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid GST should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(777),
             "GST should preserve the framing timestamp");
  ExpectUtcTime(ctx, record.utc_time, 12u, 35u, 19.0, "GST utc time");
  ctx.Expect(record.rms_range_residual_m.has_value() &&
                 NearlyEqual(*record.rms_range_residual_m, 1.2),
             "GST should decode RMS range residual");
  ctx.Expect(record.semi_major_std_dev_m.has_value() &&
                 NearlyEqual(*record.semi_major_std_dev_m, 0.8),
             "GST should decode semi-major standard deviation");
  ctx.Expect(record.semi_minor_std_dev_m.has_value() &&
                 NearlyEqual(*record.semi_minor_std_dev_m, 0.7),
             "GST should decode semi-minor standard deviation");
  ctx.Expect(record.orientation_deg.has_value() && NearlyEqual(*record.orientation_deg, 45.0),
             "GST should decode ellipse orientation");
  ctx.Expect(record.latitude_std_dev_m.has_value() &&
                 NearlyEqual(*record.latitude_std_dev_m, 0.5),
             "GST should decode latitude standard deviation");
  ctx.Expect(record.longitude_std_dev_m.has_value() &&
                 NearlyEqual(*record.longitude_std_dev_m, 0.6),
             "GST should decode longitude standard deviation");
  ctx.Expect(record.altitude_std_dev_m.has_value() &&
                 NearlyEqual(*record.altitude_std_dev_m, 1.1),
             "GST should decode altitude standard deviation");
}

void TestValidVtgParsing(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPVTG,054.7,T,034.4,M,005.5,N,010.2,K,A"),
      889);
  const auto result = universal_gnss_protocols::ParseNmeaVtg(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid VTG should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(889),
             "VTG should preserve the framing timestamp");
  ctx.Expect(record.true_course_deg.has_value() && NearlyEqual(*record.true_course_deg, 54.7),
             "VTG should decode true course");
  ctx.Expect(record.magnetic_course_deg.has_value() &&
                 NearlyEqual(*record.magnetic_course_deg, 34.4),
             "VTG should decode magnetic course");
  ctx.Expect(record.speed_knots.has_value() && NearlyEqual(*record.speed_knots, 5.5),
             "VTG should decode speed in knots");
  ctx.Expect(record.speed_kmh.has_value() && NearlyEqual(*record.speed_kmh, 10.2),
             "VTG should decode speed in km/h");
  ctx.Expect(record.mode_indicator == std::optional<char>('A'),
             "VTG should decode the mode indicator when present");
}

void TestVtgMissingOptionalFields(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPVTG,054.7,T,,M,005.5,N,010.2,K"), 890);
  const auto result = universal_gnss_protocols::ParseNmeaVtg(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "VTG should tolerate missing optional magnetic course and mode fields");
  if (!result.record.has_value())
  {
    return;
  }

  ctx.Expect(result.record->true_course_deg.has_value() &&
                 NearlyEqual(*result.record->true_course_deg, 54.7),
             "VTG should still decode true course");
  ctx.Expect(!result.record->magnetic_course_deg.has_value(),
             "missing VTG magnetic course should stay unset");
  ctx.Expect(result.record->speed_knots.has_value() &&
                 NearlyEqual(*result.record->speed_knots, 5.5),
             "VTG should still decode knots when magnetic course is missing");
  ctx.Expect(result.record->speed_kmh.has_value() &&
                 NearlyEqual(*result.record->speed_kmh, 10.2),
             "VTG should still decode km/h when mode is absent");
  ctx.Expect(!result.record->mode_indicator.has_value(),
             "missing VTG mode indicator should stay unset");
}

void TestVtgRuntimeMapping(TestContext& ctx)
{
  const auto parsed = universal_gnss_protocols::ParseNmeaVtg(
      FrameSentence(MakeSentence("GPVTG,054.7,T,034.4,M,005.5,N,010.2,K,A"), 891));
  ctx.Expect(parsed.record.has_value(), "VTG runtime mapping test requires a parsed record");
  if (!parsed.record.has_value())
  {
    return;
  }

  const auto state = universal_gnss_protocols::NmeaVtgToRuntimeState(*parsed.record);
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(891) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kSpeedOverGround) &&
                 NearlyEqual(*state.speed_over_ground_m_s, 5.5 * 0.514444) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kCourseOverGround) &&
                 state.course_over_ground_deg == std::optional<float>(54.7f),
             "VTG runtime mapping should expose SI ground speed and true course");
  ctx.Expect(!state.heading_deg.has_value(),
             "VTG runtime mapping must not reinterpret course as heading");

  universal_gnss_protocols::NmeaVtgRecord kmh_only;
  kmh_only.speed_kmh = 36.0f;
  const auto kmh_state = universal_gnss_protocols::NmeaVtgToRuntimeState(kmh_only);
  ctx.Expect(kmh_state.speed_over_ground_m_s == std::optional<float>(10.0f) &&
                 !kmh_state.course_over_ground_deg.has_value() &&
                 universal_gnss::HasValueAvailable(kmh_state, GnssCapability::kSpeedOverGround) &&
                 !universal_gnss::HasValueAvailable(kmh_state, GnssCapability::kCourseOverGround),
             "VTG should fall back to km/h and clear unavailable true course independently");
}

void TestMalformedVtgRejected(TestContext& ctx)
{
  const NmeaSentence checksum_sentence =
      FrameSentence("$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K,A*00\r\n");
  ctx.Expect(checksum_sentence.checksum_status == ChecksumStatus::kInvalid,
             "test setup should produce an invalid VTG checksum");
  ctx.Expect(universal_gnss_protocols::ParseNmeaVtg(checksum_sentence).status ==
                 ParserStatus::kInvalidData,
             "VTG semantic parsing should reject invalid checksums");

  const NmeaSentence numeric_sentence =
      FrameSentence(MakeSentence("GPVTG,abc,T,034.4,M,005.5,N,010.2,K,A"));
  ctx.Expect(universal_gnss_protocols::ParseNmeaVtg(numeric_sentence).status ==
                 ParserStatus::kInvalidData,
             "VTG should reject malformed numeric fields");
}

void TestValidZdaParsing(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPZDA,201530.00,04,07,2002,02,30"),
      891);
  const auto result = universal_gnss_protocols::ParseNmeaZda(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid ZDA should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.timestamp_ns == std::optional<std::int64_t>(891),
             "ZDA should preserve the framing timestamp");
  ExpectUtcTime(ctx, record.utc_time, 20u, 15u, 30.0, "ZDA utc time");
  ctx.Expect(record.day == 4u && record.month == 7u && record.year == 2002u,
             "ZDA should decode the UTC calendar date");
  ctx.Expect(record.local_zone_hours == std::optional<std::int8_t>(2) &&
                 record.local_zone_minutes == std::optional<std::int8_t>(30),
             "ZDA should decode the local zone offset");
}

void TestZdaMissingLocalZoneIsTolerated(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPZDA,172809.456,12,07,1996,,"),
      892);
  const auto result = universal_gnss_protocols::ParseNmeaZda(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "ZDA should tolerate missing local zone fields");
  if (!result.record.has_value())
  {
    return;
  }

  ExpectUtcTime(ctx, result.record->utc_time, 17u, 28u, 9.456, "ZDA utc time");
  ctx.Expect(result.record->day == 12u && result.record->month == 7u &&
                 result.record->year == 1996u,
             "ZDA should still decode the UTC date when local zone is missing");
  ctx.Expect(!result.record->local_zone_hours.has_value() &&
                 !result.record->local_zone_minutes.has_value(),
             "missing local zone fields should stay unset");
}

void TestMalformedZdaRejected(TestContext& ctx)
{
  const NmeaSentence checksum_sentence =
      FrameSentence("$GPZDA,201530.00,04,07,2002,02,30*00\r\n");
  ctx.Expect(checksum_sentence.checksum_status == ChecksumStatus::kInvalid,
             "test setup should produce an invalid ZDA checksum");
  ctx.Expect(universal_gnss_protocols::ParseNmeaZda(checksum_sentence).status ==
                 ParserStatus::kInvalidData,
             "ZDA semantic parsing should reject invalid checksums");

  const NmeaSentence invalid_date_sentence =
      FrameSentence(MakeSentence("GPZDA,201530.00,32,07,2002,02,30"));
  ctx.Expect(universal_gnss_protocols::ParseNmeaZda(invalid_date_sentence).status ==
                 ParserStatus::kInvalidData,
             "ZDA should reject invalid calendar dates");

  const NmeaSentence malformed_numeric_sentence =
      FrameSentence(MakeSentence("GPZDA,201530.00,04,07,20xx,02,30"));
  ctx.Expect(universal_gnss_protocols::ParseNmeaZda(malformed_numeric_sentence).status ==
                 ParserStatus::kInvalidData,
             "ZDA should reject malformed numeric fields");
}

void TestGstMissingOptionalFields(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGST,123520.00,,,,,,,"));
  const auto result = universal_gnss_protocols::ParseNmeaGst(sentence);

  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "GST should tolerate missing optional accuracy fields");
  if (!result.record.has_value())
  {
    return;
  }

  ExpectUtcTime(ctx, result.record->utc_time, 12u, 35u, 20.0, "GST utc time");
  ctx.Expect(!result.record->rms_range_residual_m.has_value() &&
                 !result.record->semi_major_std_dev_m.has_value() &&
                 !result.record->semi_minor_std_dev_m.has_value() &&
                 !result.record->orientation_deg.has_value() &&
                 !result.record->latitude_std_dev_m.has_value() &&
                 !result.record->longitude_std_dev_m.has_value() &&
                 !result.record->altitude_std_dev_m.has_value(),
             "missing GST optional fields should stay unset");
}

void TestMalformedGstRejected(TestContext& ctx)
{
  const NmeaSentence checksum_sentence =
      FrameSentence("$GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,0.6,1.1*00\r\n");
  ctx.Expect(checksum_sentence.checksum_status == ChecksumStatus::kInvalid,
             "test setup should produce an invalid GST checksum");
  ctx.Expect(universal_gnss_protocols::ParseNmeaGst(checksum_sentence).status ==
                 ParserStatus::kInvalidData,
             "GST semantic parsing should reject invalid checksums");

  const NmeaSentence numeric_sentence = FrameSentence(
      MakeSentence("GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,abc,1.1"));
  ctx.Expect(universal_gnss_protocols::ParseNmeaGst(numeric_sentence).status ==
                 ParserStatus::kInvalidData,
             "GST should reject malformed numeric fields");
}

void TestGsaAndGsvRuntimeMapping(TestContext& ctx)
{
  const NmeaSentence gsa_sentence = FrameSentence(
      MakeSentence("GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5"),
      555);
  const auto gsa_result = universal_gnss_protocols::ParseNmeaGsa(gsa_sentence);
  ctx.Expect(gsa_result.record.has_value(), "runtime mapping test requires a parsed GSA record");
  if (!gsa_result.record.has_value())
  {
    return;
  }

  const auto gsa_state = universal_gnss_protocols::NmeaGsaToRuntimeState(*gsa_result.record);
  ctx.Expect(gsa_state.timestamp_ns == std::optional<std::int64_t>(555),
             "GSA runtime mapping should preserve the sample timestamp");
  ctx.Expect(gsa_state.fix_valid && gsa_state.fix_type == GnssFixType::kFix,
             "GSA runtime mapping should turn 3D fix dimension into a generic fix");
  ctx.Expect(universal_gnss::HasCapability(gsa_state, GnssCapability::kHdop) &&
                 universal_gnss::HasCapability(gsa_state, GnssCapability::kVdop) &&
                 universal_gnss::HasCapability(gsa_state, GnssCapability::kSatellitesUsed),
             "GSA runtime mapping should advertise supported optional fields");
  ctx.Expect(universal_gnss::HasValueAvailable(gsa_state, GnssCapability::kHdop) &&
                 universal_gnss::HasValueAvailable(gsa_state, GnssCapability::kVdop) &&
                 universal_gnss::HasValueAvailable(gsa_state, GnssCapability::kSatellitesUsed),
             "GSA runtime mapping should expose present optional values");
  ctx.Expect(gsa_state.satellites_used == std::optional<std::uint16_t>(8u),
             "GSA runtime mapping should count active satellites");

  const NmeaSentence gsv_sentence = FrameSentence(
      MakeSentence("GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"),
      666);
  const auto gsv_result = universal_gnss_protocols::ParseNmeaGsv(gsv_sentence);
  ctx.Expect(gsv_result.record.has_value(), "runtime mapping test requires a parsed GSV record");
  if (!gsv_result.record.has_value())
  {
    return;
  }

  universal_gnss::GnssRuntimeState state;
  universal_gnss_protocols::MergeNmeaGsvIntoRuntimeState(*gsv_result.record, state);
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(666),
             "GSV runtime merge should preserve the sample timestamp");
  ctx.Expect(universal_gnss::HasCapability(state, GnssCapability::kSatellitesVisible) &&
                 universal_gnss::HasCapability(state, GnssCapability::kMeanCn0) &&
                 universal_gnss::HasCapability(state, GnssCapability::kMaxCn0),
             "GSV runtime merge should advertise visibility and CN0 capabilities");
  ctx.Expect(state.satellites_visible == std::optional<std::uint16_t>(8u),
             "GSV runtime merge should expose satellites visible");
  ctx.Expect(state.mean_cn0_db_hz.has_value() && NearlyEqual(*state.mean_cn0_db_hz, 41.25),
             "GSV runtime merge should compute mean CN0 from valid satellites in the sentence");
  ctx.Expect(state.max_cn0_db_hz.has_value() && NearlyEqual(*state.max_cn0_db_hz, 43.0),
             "GSV runtime merge should compute max CN0 from valid satellites in the sentence");
  ctx.Expect(!universal_gnss::HasCapability(state, GnssCapability::kRtkMode),
             "GSV runtime merge should not invent RTK capability");
}

void TestGstRuntimeMapping(TestContext& ctx)
{
  const NmeaSentence sentence = FrameSentence(
      MakeSentence("GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,0.6,1.1"),
      888);
  const auto result = universal_gnss_protocols::ParseNmeaGst(sentence);
  ctx.Expect(result.record.has_value(), "runtime mapping test requires a parsed GST record");
  if (!result.record.has_value())
  {
    return;
  }

  const auto state = universal_gnss_protocols::NmeaGstToRuntimeState(*result.record);
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(888),
             "GST runtime mapping should preserve the sample timestamp");
  ctx.Expect(universal_gnss::HasCapability(state, GnssCapability::kHorizontalAccuracy) &&
                 universal_gnss::HasCapability(state, GnssCapability::kVerticalAccuracy),
             "GST runtime mapping should advertise accuracy capabilities");
  ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kHorizontalAccuracy) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kVerticalAccuracy),
             "GST runtime mapping should expose derived accuracy values when present");
  ctx.Expect(state.horizontal_accuracy_m.has_value() &&
                 NearlyEqual(*state.horizontal_accuracy_m, 0.6),
             "GST runtime mapping should conservatively use the worst horizontal axis");
  ctx.Expect(state.vertical_accuracy_m.has_value() &&
                 NearlyEqual(*state.vertical_accuracy_m, 1.1),
             "GST runtime mapping should expose altitude standard deviation as vertical accuracy");
  ctx.Expect(!state.fix_valid && state.fix_type == GnssFixType::kUnknown,
             "GST runtime mapping should not invent fix validity");
  ctx.Expect(!universal_gnss::HasCapability(state, GnssCapability::kRtkMode) &&
                 !universal_gnss::HasCapability(state, GnssCapability::kSatellitesUsed) &&
                 !universal_gnss::HasCapability(state, GnssCapability::kMeanCn0),
             "GST runtime mapping should not invent RTK, satellite, or CN0 capabilities");

  universal_gnss::GnssRuntimeState merged_state;
  merged_state.latitude_deg = 48.1173;
  merged_state.longitude_deg = 11.5166667;
  merged_state.fix_valid = true;
  merged_state.fix_type = GnssFixType::kFix;
  universal_gnss_protocols::MergeNmeaGstIntoRuntimeState(*result.record, merged_state);

  ctx.Expect(merged_state.latitude_deg.has_value() && NearlyEqual(*merged_state.latitude_deg, 48.1173) &&
                 merged_state.longitude_deg.has_value() &&
                 NearlyEqual(*merged_state.longitude_deg, 11.5166667),
             "GST runtime merge should not overwrite position");
  ctx.Expect(merged_state.fix_valid && merged_state.fix_type == GnssFixType::kFix,
             "GST runtime merge should not overwrite fix state");
}

void TestNmeaPartialStatesCanBeAggregated(TestContext& ctx)
{
  const NmeaSentence gga_sentence = FrameSentence(
      MakeSentence("GPGGA,123519,4807.038,N,01131.000,E,5,08,0.9,545.4,M,46.9,M,,"),
      1000);
  const NmeaSentence gsa_sentence = FrameSentence(
      MakeSentence("GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5"),
      1010);
  const NmeaSentence gsv_sentence = FrameSentence(
      MakeSentence("GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"),
      1020);

  const auto gga = universal_gnss_protocols::ParseNmeaGga(gga_sentence);
  const auto gsa = universal_gnss_protocols::ParseNmeaGsa(gsa_sentence);
  const auto gsv = universal_gnss_protocols::ParseNmeaGsv(gsv_sentence);

  ctx.Expect(gga.record.has_value() && gsa.record.has_value() && gsv.record.has_value(),
             "NMEA aggregation test requires parsed GGA, GSA, and GSV records");
  if (!gga.record.has_value() || !gsa.record.has_value() || !gsv.record.has_value())
  {
    return;
  }

  universal_gnss::GnssRuntimeState gsv_state;
  universal_gnss_protocols::MergeNmeaGsvIntoRuntimeState(*gsv.record, gsv_state);

  GnssRuntimeAggregator aggregator;
  aggregator.Merge(universal_gnss_protocols::NmeaGgaToRuntimeState(*gga.record));
  aggregator.Merge(universal_gnss_protocols::NmeaGsaToRuntimeState(*gsa.record));
  aggregator.Merge(gsv_state);

  const universal_gnss::GnssRuntimeState& state = aggregator.state();
  ctx.Expect(state.fix_valid && state.fix_type == GnssFixType::kFix,
             "aggregated NMEA state should retain a generic valid fix");
  ctx.Expect(universal_gnss::HasCapability(state, GnssCapability::kRtkMode) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kRtkMode) &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFloat),
             "aggregated NMEA state should preserve the GGA-derived RTK mode");
  ctx.Expect(state.latitude_deg.has_value() && NearlyEqual(*state.latitude_deg, 48.1173) &&
                 state.longitude_deg.has_value() && NearlyEqual(*state.longitude_deg, 11.5166667) &&
                 state.altitude_m.has_value() && NearlyEqual(*state.altitude_m, 545.4),
             "aggregated NMEA state should retain position from GGA");
  ctx.Expect(state.hdop.has_value() && NearlyEqual(*state.hdop, 1.0) &&
                 state.vdop.has_value() && NearlyEqual(*state.vdop, 1.5),
             "aggregated NMEA state should merge GSA DOP values");
  ctx.Expect(state.satellites_used == std::optional<std::uint16_t>(8u) &&
                 state.satellites_visible == std::optional<std::uint16_t>(8u),
             "aggregated NMEA state should merge GSA and GSV satellite counts");
  ctx.Expect(state.mean_cn0_db_hz.has_value() && NearlyEqual(*state.mean_cn0_db_hz, 41.25) &&
                 state.max_cn0_db_hz.has_value() && NearlyEqual(*state.max_cn0_db_hz, 43.0),
             "aggregated NMEA state should merge per-sentence CN0 summaries");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestCoordinateConversionHelpers(ctx);
  TestValidGgaParsing(ctx);
  TestValidRmcParsing(ctx);
  TestMalformedChecksumRejected(ctx);
  TestMalformedCoordinatesRejected(ctx);
  TestMissingOptionalFieldsAreTolerated(ctx);
  TestSouthWestCoordinates(ctx);
  TestInvalidFixQualityRejected(ctx);
  TestRuntimeMappingBehavior(ctx);
  TestValidGsaParsing(ctx);
  TestValidGsvParsing(ctx);
  TestValidGstParsing(ctx);
  TestValidVtgParsing(ctx);
  TestValidZdaParsing(ctx);
  TestPartialGsvSatelliteBlockHandling(ctx);
  TestMalformedDopRejected(ctx);
  TestMissingOptionalGsaFields(ctx);
  TestGsvMissingOptionalFields(ctx);
  TestGstMissingOptionalFields(ctx);
  TestVtgMissingOptionalFields(ctx);
  TestVtgRuntimeMapping(ctx);
  TestZdaMissingLocalZoneIsTolerated(ctx);
  TestMalformedGstRejected(ctx);
  TestMalformedVtgRejected(ctx);
  TestMalformedZdaRejected(ctx);
  TestGsaAndGsvRuntimeMapping(ctx);
  TestGstRuntimeMapping(ctx);
  TestNmeaPartialStatesCanBeAggregated(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols NMEA semantic tests passed\n";
  return EXIT_SUCCESS;
}
