#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_protocols/unicore_framer.hpp"
#include "universal_gnss_protocols/unicore_parser.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss_protocols::ParseUnicoreBestNav;
using universal_gnss_protocols::ParseUnicorePvtsln;
using universal_gnss_protocols::ParseUnicoreRtkStatus;
using universal_gnss_protocols::ParseUnicoreRtcmStatus;
using universal_gnss_protocols::ParseUnicoreSatsInfo;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UnicoreFrame;
using universal_gnss_protocols::UnicoreFrameFramer;

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

UnicoreFrame BuildAsciiFrame(const std::string& line,
                             const universal_gnss_protocols::ProtocolTimestampNs timestamp_ns = 0)
{
  UnicoreFrameFramer framer;
  for (const char ch : line)
  {
    const auto result = framer.PushByte(static_cast<std::uint8_t>(ch), timestamp_ns);
    if (result.status == ParserStatus::kRecordReady && result.record.has_value())
    {
      return *result.record;
    }
  }

  const auto result = framer.Finalize();
  if (result.record.has_value())
  {
    return *result.record;
  }

  std::cerr << "FAILED: could not frame test Unicore line\n";
  std::exit(EXIT_FAILURE);
}

void TestPvtslnParsingAndRuntimeMapping(TestContext& ctx)
{
  const std::string line =
      "#PVTSLNA,97,GPS,FINE,2190,364536000,0,0,18,13;"
      "NARROW_INT,60.5060,40.07898130522,116.23663134427,0.2000,0.1500,0.1800,0.9000,"
      "SINGLE,60.5060,40.07898130522,116.23663134427,4.3353,46,28,46,28,0.0009,-0.0031,-0.0032,"
      "SOL_COMPUTED,1.5000,182.2500,0.1000,28,25,12,8,2.1753,1.3480,0.6840,1.8392,1.7072,5.0,"
      "28,25,26*1e33c8cb\r\n";

  const auto result = ParseUnicorePvtsln(BuildAsciiFrame(line, 1111));
  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid PVTSLNA line should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.header.timestamp_ns == 1111 &&
                 record.header.gps_week == 2190u &&
                 record.header.gps_millis_of_week == 364536000u,
             "PVTSLNA should preserve the framing timestamp and parse the ASCII header");
  ctx.Expect(record.best_tracked_satellites == 46u &&
                 record.best_used_satellites == 28u &&
                 record.hdop.has_value() &&
                 *record.hdop == 0.6840f,
             "PVTSLNA should parse tracked/used satellites and HDOP");

  const auto state = universal_gnss_protocols::UnicorePvtslnToRuntimeState(record);
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFixed &&
                 state.rtk_mode == GnssRtkMode::kFixed,
             "PVTSLNA runtime mapping should expose RTK fixed state from NARROW_INT");
  ctx.Expect(state.latitude_deg == 40.07898130522 &&
                 state.longitude_deg == 116.23663134427 &&
                 state.altitude_m == 60.5060,
             "PVTSLNA runtime mapping should use the best-position coordinates");
  ctx.Expect(universal_gnss::HasCapability(state, GnssCapability::kHorizontalAccuracy) &&
                 universal_gnss::HasCapability(state, GnssCapability::kVerticalAccuracy) &&
                 universal_gnss::HasCapability(state, GnssCapability::kCorrectionAge) &&
                 universal_gnss::HasCapability(state, GnssCapability::kHeading),
             "PVTSLNA runtime mapping should advertise supported optional fields");
  ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kHorizontalAccuracy) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kVerticalAccuracy) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kCorrectionAge) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kHeading),
             "PVTSLNA runtime mapping should expose present optional values");
  ctx.Expect(!universal_gnss::HasCapability(state, GnssCapability::kInterferenceState) &&
                 !universal_gnss::HasCapability(state, GnssCapability::kJammingState),
             "PVTSLNA should not invent RF or jamming capabilities");
}

void TestBestNavParsingAndMapping(TestContext& ctx)
{
  const std::string line =
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
      "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
      "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n";

  const auto result = ParseUnicoreBestNav(BuildAsciiFrame(line, 2222));
  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid BESTNAVA line should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.solution_status ==
                 universal_gnss_protocols::UnicoreSolutionStatus::kSolComputed &&
                 record.position_type ==
                     universal_gnss_protocols::UnicorePositionType::kNarrowFloat &&
                 record.datum_is_wgs84.has_value() && *record.datum_is_wgs84 &&
                 record.diff_age_s == 0.4f &&
                 record.tracked_satellites == 50u &&
                 record.used_satellites == 28u,
             "BESTNAVA should parse the stable position, age, and satellite fields");

  const auto state = universal_gnss_protocols::UnicoreBestNavToRuntimeState(record);
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFloat &&
                 state.rtk_mode == GnssRtkMode::kFloat,
             "BESTNAVA runtime mapping should expose RTK float state from NARROW_FLOAT");
  ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kCorrectionAge) &&
                 state.correction_age_s == 0.4f,
             "BESTNAVA runtime mapping should expose the documented differential age");
  ctx.Expect(!universal_gnss::HasCapability(state, GnssCapability::kHeading),
             "BESTNAVA should not invent heading capabilities");
}

void TestRtkStatusAndRtcmStatusParsing(TestContext& ctx)
{
  const std::string rtk_fixed_line =
      "#RTKSTATUSA,97,GPS,FINE,2190,365354000,0,0,18,1;"
      "0,0,0,0,0,0,0,0,0,0,0,NARROW_INT,5,0,1,12,0*f06a8a06\r\n";
  const auto rtk_fixed_result = ParseUnicoreRtkStatus(BuildAsciiFrame(rtk_fixed_line, 3333));
  ctx.Expect(rtk_fixed_result.status == ParserStatus::kRecordReady &&
                 rtk_fixed_result.record.has_value(),
             "valid RTKSTATUSA line should parse successfully");
  if (rtk_fixed_result.record.has_value())
  {
    const auto state = universal_gnss_protocols::UnicoreRtkStatusToRuntimeState(
        *rtk_fixed_result.record);
    ctx.Expect(state.fix_valid &&
                   state.fix_type == GnssFixType::kRtkFixed &&
                   state.rtk_mode == GnssRtkMode::kFixed,
               "RTKSTATUSA runtime mapping should expose RTK fixed state");
    ctx.Expect(universal_gnss::HasCapability(state, GnssCapability::kDualAntennaHeading) &&
                   universal_gnss::HasValueAvailable(state, GnssCapability::kDualAntennaHeading) &&
                   state.dual_antenna_heading == true,
               "RTKSTATUSA should map the documented within-limit dual-antenna flag");
  }

  const std::string rtk_float_line =
      "#RTKSTATUSA,97,GPS,FINE,2190,365355000,0,0,18,1;"
      "0,0,0,0,0,0,0,0,0,0,0,NARROW_FLOAT,5,0,2,10,0*11111111\r\n";
  const auto rtk_float_result = ParseUnicoreRtkStatus(BuildAsciiFrame(rtk_float_line, 3334));
  ctx.Expect(rtk_float_result.status == ParserStatus::kRecordReady &&
                 rtk_float_result.record.has_value(),
             "second RTKSTATUSA line should parse successfully");
  if (rtk_float_result.record.has_value())
  {
    const auto state = universal_gnss_protocols::UnicoreRtkStatusToRuntimeState(
        *rtk_float_result.record);
    ctx.Expect(state.fix_valid &&
                   state.fix_type == GnssFixType::kRtkFloat &&
                   state.rtk_mode == GnssRtkMode::kFloat &&
                   state.dual_antenna_heading == false,
               "RTKSTATUSA should expose float RTK and a non-within-limit dual-antenna state");
  }

  const std::string rtcm_line =
      "#RTCMSTATUSA,76,GPS,FINE,2219,392572000,0,0,18,187;"
      "1124,21186,0,21,0,6,11,0,0,21*601a7581\r\n";
  const auto rtcm_result = ParseUnicoreRtcmStatus(BuildAsciiFrame(rtcm_line, 4444));
  ctx.Expect(rtcm_result.status == ParserStatus::kRecordReady && rtcm_result.record.has_value(),
             "valid RTCMSTATUSA line should parse successfully");
  if (rtcm_result.record.has_value())
  {
    const auto& record = *rtcm_result.record;
    ctx.Expect(record.message_type == 1124u &&
                   record.message_count == 21186u &&
                   record.satellites_in_message == 21u &&
                   record.l3_observables == 11u,
               "RTCMSTATUSA should parse the stable RTCM message counters");
    const auto state = universal_gnss_protocols::UnicoreRtcmStatusToRuntimeState(record);
    ctx.Expect(!universal_gnss::HasCapability(state, GnssCapability::kCorrectionAge) &&
                   !universal_gnss::HasCapability(state, GnssCapability::kSatellitesUsed),
               "RTCMSTATUSA should not invent runtime capabilities that the core does not model");
  }
}

void TestSatsInfoParsingAndRuntimeMapping(TestContext& ctx)
{
  const std::string line =
      "#SATSINFOA,96,GPS,FINE,2215,367199000,0,0,18,16;"
      "3,2,0,0,0,63,"
      "2,302,51,0,45,0,2,0,42,9,2,"
      "4,48,17,0,37,0,3,0,43,14,3,0,39,9,3,"
      "5,225,14,1,50,0,1*abcdef12\r\n";

  const auto result = ParseUnicoreSatsInfo(BuildAsciiFrame(line, 5555));
  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid SATSINFOA line should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.header.timestamp_ns == 5555 &&
                 record.tracked_satellite_count == 3u &&
                 record.parsed_satellite_count == 3u &&
                 record.frequency_flag == 63u,
             "SATSINFOA should parse the common header and tracked-satellite count");
  ctx.Expect(record.satellites[0].satellite_id == 2u &&
                 record.satellites[0].azimuth_deg == 302 &&
                 record.satellites[0].elevation_deg == 51 &&
                 record.satellites[0].cn0_db_hz == 45u &&
                 record.satellites[1].frequency_count == 3u &&
                 record.satellites[1].cn0_db_hz == 43u &&
                 record.satellites[2].system_id == 1u,
             "SATSINFOA should keep the stable per-satellite identity and CN0 summary");

  const auto state = universal_gnss_protocols::UnicoreSatsInfoToRuntimeState(record);
  ctx.Expect(universal_gnss::HasCapability(state, GnssCapability::kSatellitesTracked) &&
                 universal_gnss::HasCapability(state, GnssCapability::kMeanCn0) &&
                 universal_gnss::HasCapability(state, GnssCapability::kMaxCn0),
             "SATSINFOA runtime mapping should advertise tracked-satellite and CN0 support");
  ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kSatellitesTracked) &&
                 state.satellites_tracked == 3u &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kMeanCn0) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kMaxCn0) &&
                 state.mean_cn0_db_hz == 46.0f &&
                 state.max_cn0_db_hz == 50.0f,
             "SATSINFOA runtime mapping should expose tracked count and mean/max CN0");
  ctx.Expect(!universal_gnss::HasCapability(state, GnssCapability::kRtkMode) &&
                 !universal_gnss::HasCapability(state, GnssCapability::kCorrectionAge) &&
                 !universal_gnss::HasCapability(state, GnssCapability::kInterferenceState),
             "SATSINFOA should not invent RTK, correction, or RF capabilities");
}

void TestMalformedAndMissingFields(TestContext& ctx)
{
  const std::string malformed_bestnav =
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,SINGLE,not_a_latitude,116.2365102982,65.8312*abcd1234\r\n";
  const auto malformed_result = ParseUnicoreBestNav(BuildAsciiFrame(malformed_bestnav));
  ctx.Expect(malformed_result.status == ParserStatus::kInvalidData,
             "malformed BESTNAVA numerics should be rejected");

  const std::string partial_pvtsln =
      "#PVTSLNA,97,GPS,FINE,2190,364536000,0,0,18,13;"
      "SINGLE,60.5060,40.07898130522,116.23663134427,0.2000,0.1500,0.1800,0.0000,"
      "SINGLE,60.5060,40.07898130522,116.23663134427,4.3353,46,28,46,28*12345678\r\n";
  const auto partial_result = ParseUnicorePvtsln(BuildAsciiFrame(partial_pvtsln));
  ctx.Expect(partial_result.status == ParserStatus::kRecordReady &&
                 partial_result.record.has_value(),
             "PVTSLNA should tolerate missing trailing optional heading and DOP fields");
  if (partial_result.record.has_value())
  {
    const auto state = universal_gnss_protocols::UnicorePvtslnToRuntimeState(*partial_result.record);
    ctx.Expect(!universal_gnss::HasValueAvailable(state, GnssCapability::kHeading) &&
                   !universal_gnss::HasValueAvailable(state, GnssCapability::kHdop),
               "missing trailing optional fields should stay unset in runtime mapping");
  }

  const std::string malformed_satsinfo =
      "#SATSINFOA,96,GPS,FINE,2215,367199000,0,0,18,16;"
      "1,2,0,0,0,63,2,302,51,0,45,0,0*badc0de0\r\n";
  const auto malformed_satsinfo_result = ParseUnicoreSatsInfo(BuildAsciiFrame(malformed_satsinfo));
  ctx.Expect(malformed_satsinfo_result.status == ParserStatus::kInvalidData,
             "SATSINFOA should reject malformed essentials like a zero frequency count");

  const std::string empty_satsinfo =
      "#SATSINFOA,96,GPS,FINE,2215,367199000,0,0,18,16;0,2,0,0,0,63*12345678\r\n";
  const auto empty_satsinfo_result = ParseUnicoreSatsInfo(BuildAsciiFrame(empty_satsinfo, 6666));
  ctx.Expect(empty_satsinfo_result.status == ParserStatus::kRecordReady &&
                 empty_satsinfo_result.record.has_value(),
             "SATSINFOA should accept an empty satellite list");
  if (empty_satsinfo_result.record.has_value())
  {
    const auto state =
        universal_gnss_protocols::UnicoreSatsInfoToRuntimeState(*empty_satsinfo_result.record);
    ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kSatellitesTracked) &&
                   state.satellites_tracked == 0u &&
                   !universal_gnss::HasValueAvailable(state, GnssCapability::kMeanCn0) &&
                   !universal_gnss::HasValueAvailable(state, GnssCapability::kMaxCn0),
               "empty SATSINFOA should keep tracked count but leave CN0 values unset");
  }
}

}  // namespace

int main()
{
  TestContext ctx;

  TestPvtslnParsingAndRuntimeMapping(ctx);
  TestBestNavParsingAndMapping(ctx);
  TestRtkStatusAndRtcmStatusParsing(ctx);
  TestSatsInfoParsingAndRuntimeMapping(ctx);
  TestMalformedAndMissingFields(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols Unicore ASCII parser tests passed\n";
  return EXIT_SUCCESS;
}
