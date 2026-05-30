#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_ntrip/gga_sentence_builder.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/nmea_parser.hpp"

namespace
{

using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::GnssRuntimeState;
using universal_gnss_ntrip::BuildNmeaGgaSentence;
using universal_gnss_ntrip::GgaSentenceBuildError;
using universal_gnss_ntrip::GgaSentenceBuilderOptions;
using universal_gnss_ntrip::GgaSentenceTalker;
using universal_gnss_ntrip::MapRuntimeStateToGgaFixQuality;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaGgaFixQuality;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::NmeaSentenceFramer;
using universal_gnss_protocols::NmeaUtcTime;
using universal_gnss_protocols::ParserStatus;

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

bool NearlyEqual(const double lhs, const double rhs, const double tolerance = 1e-5)
{
  return std::fabs(lhs - rhs) <= tolerance;
}

NmeaSentence FrameSentence(const std::string& text)
{
  NmeaSentenceFramer framer;
  universal_gnss_protocols::ParserResult<NmeaSentence> result;
  for (const char ch : text)
  {
    result = framer.PushByte(static_cast<std::uint8_t>(ch));
  }

  if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
  {
    std::cerr << "FAILED: test setup could not frame a generated GGA sentence\n";
    std::exit(EXIT_FAILURE);
  }

  return *result.record;
}

GnssRuntimeState MakeState()
{
  GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = GnssFixType::kFix;
  state.latitude_deg = 48.1173;
  state.longitude_deg = 11.5166667;
  state.altitude_m = 545.4;
  state.hdop = 0.9f;
  state.satellites_used = 8u;
  return state;
}

void TestCoordinateAndTimeFormatting(TestContext& ctx)
{
  GgaSentenceBuilderOptions options;
  options.talker = GgaSentenceTalker::kGn;
  options.utc_time = NmeaUtcTime{12u, 34u, 56.78};

  const auto generated = BuildNmeaGgaSentence(MakeState(), options);
  ctx.Expect(generated.ok(), "valid runtime state should build a GGA sentence");
  if (!generated.ok())
  {
    return;
  }

  ctx.Expect(generated.sentence.find("$GNGGA,123456.78,4807.03800,N,01131.00000,E,1,08,0.9,545.4,M") == 0,
             "generated GGA should format talker, UTC, coordinates, quality, and altitude deterministically");
}

void TestHemisphereSigns(TestContext& ctx)
{
  auto state = MakeState();
  state.latitude_deg = -49.2741667;
  state.longitude_deg = -123.1853333;

  const auto generated = BuildNmeaGgaSentence(state);
  ctx.Expect(generated.ok(), "south/west coordinates should still build a sentence");
  if (!generated.ok())
  {
    return;
  }

  ctx.Expect(generated.sentence.find(",S,") != std::string::npos &&
                 generated.sentence.find(",W,") != std::string::npos,
             "generated GGA should encode hemisphere signs");

  const auto parsed = universal_gnss_protocols::ParseNmeaGga(FrameSentence(generated.sentence));
  ctx.Expect(parsed.record.has_value() &&
                 parsed.record->latitude_deg.has_value() &&
                 parsed.record->longitude_deg.has_value() &&
                 NearlyEqual(*parsed.record->latitude_deg, -49.2741667) &&
                 NearlyEqual(*parsed.record->longitude_deg, -123.1853333),
             "negative coordinates should round-trip through the existing parser");
}

void TestChecksumAndDefaultUtc(TestContext& ctx)
{
  const auto generated = BuildNmeaGgaSentence(MakeState());
  ctx.Expect(generated.ok(), "default builder options should build a sentence");
  if (!generated.ok())
  {
    return;
  }

  ctx.Expect(generated.sentence.find("$GPGGA,000000.00,") == 0,
             "missing UTC input should default to 000000.00");
  ctx.Expect(generated.sentence.size() >= 2u &&
                 generated.sentence.substr(generated.sentence.size() - 2u) == "\r\n",
             "generated GGA should be CRLF terminated");

  const NmeaSentence sentence = FrameSentence(generated.sentence);
  ctx.Expect(sentence.checksum_status == ChecksumStatus::kValid,
             "generated GGA should carry a valid NMEA checksum");
}

void TestFixQualityMapping(TestContext& ctx)
{
  GnssRuntimeState no_fix;
  no_fix.fix_valid = false;
  no_fix.fix_type = GnssFixType::kNoFix;
  ctx.Expect(MapRuntimeStateToGgaFixQuality(no_fix) == NmeaGgaFixQuality::kInvalid,
             "no-fix runtime state should map to GGA quality 0");

  GnssRuntimeState float_rtk;
  float_rtk.fix_valid = true;
  float_rtk.fix_type = GnssFixType::kFix;
  float_rtk.rtk_mode = GnssRtkMode::kFloat;
  ctx.Expect(MapRuntimeStateToGgaFixQuality(float_rtk) == NmeaGgaFixQuality::kRtkFloat,
             "RTK float runtime state should map to GGA quality 5");

  GnssRuntimeState fixed_rtk;
  fixed_rtk.fix_valid = true;
  fixed_rtk.fix_type = GnssFixType::kRtkFixed;
  ctx.Expect(MapRuntimeStateToGgaFixQuality(fixed_rtk) == NmeaGgaFixQuality::kRtkFixed,
             "RTK fixed runtime state should map to GGA quality 4");
}

void TestMissingCoordinatesRejected(TestContext& ctx)
{
  GnssRuntimeState missing_latitude;
  missing_latitude.fix_valid = true;
  missing_latitude.fix_type = GnssFixType::kFix;
  missing_latitude.longitude_deg = 11.0;

  const auto missing_latitude_result = BuildNmeaGgaSentence(missing_latitude);
  ctx.Expect(!missing_latitude_result.ok() &&
                 missing_latitude_result.error == GgaSentenceBuildError::kMissingLatitude &&
                 missing_latitude_result.sentence.empty(),
             "missing latitude should reject GGA generation");

  GnssRuntimeState invalid_longitude;
  invalid_longitude.fix_valid = true;
  invalid_longitude.fix_type = GnssFixType::kFix;
  invalid_longitude.latitude_deg = 48.0;
  invalid_longitude.longitude_deg = 181.0;

  const auto invalid_longitude_result = BuildNmeaGgaSentence(invalid_longitude);
  ctx.Expect(!invalid_longitude_result.ok() &&
                 invalid_longitude_result.error == GgaSentenceBuildError::kInvalidLongitude &&
                 invalid_longitude_result.sentence.empty(),
             "out-of-range longitude should reject GGA generation");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestCoordinateAndTimeFormatting(ctx);
  TestHemisphereSigns(ctx);
  TestChecksumAndDefaultUtc(ctx);
  TestFixQualityMapping(ctx);
  TestMissingCoordinatesRejected(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_ntrip GGA sentence builder tests passed\n";
  return EXIT_SUCCESS;
}
