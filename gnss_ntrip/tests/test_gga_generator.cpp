#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_ntrip/gga_generator.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/nmea_parser.hpp"

namespace
{

using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::GnssRuntimeState;
using universal_gnss_ntrip::BuildNmeaGgaSentence;
using universal_gnss_ntrip::GgaGenerationError;
using universal_gnss_ntrip::MapRuntimeStateToGgaFixQuality;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaGgaFixQuality;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::NmeaSentenceFramer;
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

void TestGeneratedSentenceHasValidChecksum(TestContext& ctx)
{
  GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = GnssFixType::kFix;
  state.latitude_deg = 48.1173;
  state.longitude_deg = 11.5166667;
  state.altitude_m = 545.4;
  state.hdop = 0.9f;
  state.satellites_used = 8u;

  const auto generated = BuildNmeaGgaSentence(state);
  ctx.Expect(generated.ok(), "valid runtime state should generate a GGA sentence");
  if (!generated.ok())
  {
    return;
  }

  const NmeaSentence sentence = FrameSentence(generated.sentence);
  ctx.Expect(sentence.checksum_status == ChecksumStatus::kValid,
             "generated GGA sentences should carry a valid NMEA checksum");

  const auto parsed = universal_gnss_protocols::ParseNmeaGga(sentence);
  ctx.Expect(parsed.status == ParserStatus::kRecordReady && parsed.record.has_value(),
             "generated GGA sentences should round-trip through the existing parser");
  if (!parsed.record.has_value())
  {
    return;
  }

  ctx.Expect(parsed.record->fix_quality == NmeaGgaFixQuality::kGpsFix,
             "generic valid fixes should map to GGA GPS-fix quality");
  ctx.Expect(parsed.record->latitude_deg.has_value() &&
                 NearlyEqual(*parsed.record->latitude_deg, 48.1173),
             "generated GGA should preserve latitude");
  ctx.Expect(parsed.record->longitude_deg.has_value() &&
                 NearlyEqual(*parsed.record->longitude_deg, 11.5166667),
             "generated GGA should preserve longitude");
  ctx.Expect(parsed.record->altitude_m.has_value() &&
                 NearlyEqual(*parsed.record->altitude_m, 545.4),
             "generated GGA should preserve altitude");
  ctx.Expect(parsed.record->hdop.has_value() &&
                 NearlyEqual(*parsed.record->hdop, 0.9),
             "generated GGA should preserve HDOP");
  ctx.Expect(parsed.record->satellites_used == std::optional<std::uint16_t>(8u),
             "generated GGA should preserve satellites used");
}

void TestHemisphereHandling(TestContext& ctx)
{
  GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = GnssFixType::kFix;
  state.latitude_deg = -49.2741667;
  state.longitude_deg = -123.1853333;

  const auto generated = BuildNmeaGgaSentence(state);
  ctx.Expect(generated.ok(), "south/west coordinates should still generate a GGA sentence");
  if (!generated.ok())
  {
    return;
  }

  ctx.Expect(generated.sentence.find(",S,") != std::string::npos &&
                 generated.sentence.find(",W,") != std::string::npos,
             "generated GGA sentences should encode southern and western hemispheres");

  const auto parsed = universal_gnss_protocols::ParseNmeaGga(FrameSentence(generated.sentence));
  ctx.Expect(parsed.record.has_value() &&
                 parsed.record->latitude_deg.has_value() &&
                 parsed.record->longitude_deg.has_value() &&
                 NearlyEqual(*parsed.record->latitude_deg, -49.2741667) &&
                 NearlyEqual(*parsed.record->longitude_deg, -123.1853333),
             "generated GGA should round-trip negative coordinates through the parser");
}

void TestFixQualityMapping(TestContext& ctx)
{
  GnssRuntimeState no_fix;
  no_fix.fix_valid = false;
  no_fix.fix_type = GnssFixType::kNoFix;
  ctx.Expect(MapRuntimeStateToGgaFixQuality(no_fix) == NmeaGgaFixQuality::kInvalid,
             "runtime states without a valid fix should map to invalid GGA quality");

  GnssRuntimeState dead_reckoning;
  dead_reckoning.fix_valid = true;
  dead_reckoning.fix_type = GnssFixType::kDeadReckoning;
  ctx.Expect(MapRuntimeStateToGgaFixQuality(dead_reckoning) == NmeaGgaFixQuality::kEstimated,
             "dead-reckoning fixes should map to estimated GGA quality");

  GnssRuntimeState float_rtk;
  float_rtk.fix_valid = true;
  float_rtk.fix_type = GnssFixType::kFix;
  float_rtk.rtk_mode = GnssRtkMode::kFloat;
  ctx.Expect(MapRuntimeStateToGgaFixQuality(float_rtk) == NmeaGgaFixQuality::kRtkFloat,
             "RTK float runtime state should map to GGA RTK-float quality");

  GnssRuntimeState fixed_rtk;
  fixed_rtk.fix_valid = true;
  fixed_rtk.fix_type = GnssFixType::kRtkFixed;
  ctx.Expect(MapRuntimeStateToGgaFixQuality(fixed_rtk) == NmeaGgaFixQuality::kRtkFixed,
             "RTK fixed runtime state should map to GGA RTK-fixed quality");
}

void TestInvalidStateHandling(TestContext& ctx)
{
  GnssRuntimeState missing_latitude;
  missing_latitude.fix_valid = true;
  missing_latitude.fix_type = GnssFixType::kFix;
  missing_latitude.longitude_deg = 11.0;

  const auto missing_latitude_result = BuildNmeaGgaSentence(missing_latitude);
  ctx.Expect(!missing_latitude_result.ok() &&
                 missing_latitude_result.error == GgaGenerationError::kMissingLatitude &&
                 missing_latitude_result.sentence.empty(),
             "missing latitude should reject GGA generation");

  GnssRuntimeState invalid_longitude;
  invalid_longitude.fix_valid = true;
  invalid_longitude.fix_type = GnssFixType::kFix;
  invalid_longitude.latitude_deg = 48.0;
  invalid_longitude.longitude_deg = 181.0;

  const auto invalid_longitude_result = BuildNmeaGgaSentence(invalid_longitude);
  ctx.Expect(!invalid_longitude_result.ok() &&
                 invalid_longitude_result.error == GgaGenerationError::kInvalidLongitude &&
                 invalid_longitude_result.sentence.empty(),
             "out-of-range longitude should reject GGA generation");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestGeneratedSentenceHasValidChecksum(ctx);
  TestHemisphereHandling(ctx);
  TestFixQualityMapping(ctx);
  TestInvalidStateHandling(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_ntrip GGA generator tests passed\n";
  return EXIT_SUCCESS;
}
