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
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/nmea_parser.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaDate;
using universal_gnss_protocols::NmeaGgaFixQuality;
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
  ctx.Expect(!universal_gnss::HasCapability(gga_state, GnssCapability::kRtkMode),
             "GGA runtime mapping should not invent RTK capability");
  ctx.Expect(gga_state.rtk_mode == std::nullopt,
             "GGA runtime mapping should not invent RTK mode");
  ctx.Expect(!gga_state.heading_deg.has_value(),
             "GGA runtime mapping should not invent heading");

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
  ctx.Expect(rmc_state.capability_flags == 0u && rmc_state.value_flags == 0u,
             "RMC runtime mapping should not invent optional capability/value flags");
  ctx.Expect(!rmc_state.altitude_m.has_value(),
             "RMC runtime mapping should not invent altitude");
  ctx.Expect(!rmc_state.heading_deg.has_value(),
             "RMC runtime mapping should not map course over ground to heading");
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

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols NMEA semantic tests passed\n";
  return EXIT_SUCCESS;
}
