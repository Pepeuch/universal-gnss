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
using universal_gnss_protocols::ParseUnicoreBestSat;
using universal_gnss_protocols::ParseUnicoreBestNav;
using universal_gnss_protocols::ParseUnicoreFreqJamStatus;
using universal_gnss_protocols::ParseUnicoreHwStatus;
using universal_gnss_protocols::ParseUnicoreJamStatus;
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
                 universal_gnss::HasCapability(state, GnssCapability::kDifferentialCorrections) &&
                 universal_gnss::HasCapability(state, GnssCapability::kCorrectionsActive) &&
                 universal_gnss::HasCapability(state, GnssCapability::kHeading),
             "PVTSLNA runtime mapping should advertise supported optional fields");
  ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kHorizontalAccuracy) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kVerticalAccuracy) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kCorrectionAge) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kDifferentialCorrections) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kCorrectionsActive) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kHeading),
             "PVTSLNA runtime mapping should expose present optional values");
  ctx.Expect(state.differential_corrections == std::optional<bool>(true) &&
                 state.corrections_active == std::optional<bool>(true),
             "PVTSLNA RTK solutions should expose known true correction state");
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
  ctx.Expect(state.differential_corrections == std::optional<bool>(true) &&
                 state.corrections_active == std::optional<bool>(true),
             "BESTNAVA RTK float solutions should expose known true correction state");
  ctx.Expect(!universal_gnss::HasCapability(state, GnssCapability::kHeading),
             "BESTNAVA should not invent heading capabilities");
}

void TestBestNavCorrectionStateSemantics(TestContext& ctx)
{
  const std::string single_line =
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,SINGLE,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
      "2.1970,\"0\",,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
      "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*00000000\r\n";
  const auto single_result = ParseUnicoreBestNav(BuildAsciiFrame(single_line, 2223));
  ctx.Expect(single_result.status == ParserStatus::kRecordReady && single_result.record.has_value(),
             "single BESTNAVA line should parse successfully");
  if (single_result.record.has_value())
  {
    const auto state = universal_gnss_protocols::UnicoreBestNavToRuntimeState(
        *single_result.record);
    ctx.Expect(state.fix_valid && state.fix_type == GnssFixType::kFix &&
                   state.rtk_mode == GnssRtkMode::kNone,
               "SINGLE BESTNAVA should expose a plain GPS fix with explicit RTK none");
    ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kDifferentialCorrections) &&
                   universal_gnss::HasValueAvailable(state, GnssCapability::kCorrectionsActive) &&
                   state.differential_corrections == std::optional<bool>(false) &&
                   state.corrections_active == std::optional<bool>(false),
               "plain SINGLE fixes should expose known false correction state");
  }

  const std::string psrdiff_line =
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,PSRDIFF,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
      "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
      "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*00000000\r\n";
  const auto psrdiff_result = ParseUnicoreBestNav(BuildAsciiFrame(psrdiff_line, 2224));
  ctx.Expect(psrdiff_result.status == ParserStatus::kRecordReady &&
                 psrdiff_result.record.has_value(),
             "PSRDIFF BESTNAVA line should parse successfully");
  if (psrdiff_result.record.has_value())
  {
    const auto state = universal_gnss_protocols::UnicoreBestNavToRuntimeState(
        *psrdiff_result.record);
    ctx.Expect(state.fix_valid && state.fix_type == GnssFixType::kFix &&
                   state.rtk_mode == GnssRtkMode::kNone,
               "PSRDIFF BESTNAVA should keep RTK mode NONE while still reporting a valid fix");
    ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kDifferentialCorrections) &&
                   universal_gnss::HasValueAvailable(state, GnssCapability::kCorrectionsActive) &&
                   state.differential_corrections == std::optional<bool>(true) &&
                   state.corrections_active == std::optional<bool>(true),
               "PSRDIFF should expose known true correction state without pretending RTK");
  }
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

void TestBestSatParsingAndRuntimeMapping(TestContext& ctx)
{
  const std::string line =
      "#BESTSATA,79,GPS,FINE,2203,226245800,0,0,18,22;"
      "4,GPS,2,GOOD,00000013,GLONASS,2-4,GOOD,00000010,GALILEO,5,GOOD,00000001,BEIDOU,20,GOOD,00000000*12345678\r\n";

  const auto result = ParseUnicoreBestSat(BuildAsciiFrame(line, 5656));
  ctx.Expect(result.status == ParserStatus::kRecordReady && result.record.has_value(),
             "valid BESTSATA line should parse successfully");
  if (!result.record.has_value())
  {
    return;
  }

  const auto& record = *result.record;
  ctx.Expect(record.header.timestamp_ns == 5656 &&
                 record.entry_count == 4u &&
                 record.parsed_satellite_count == 4u,
             "BESTSATA should preserve timestamp and parse the documented entry count");
  ctx.Expect(record.satellites[0].constellation ==
                 universal_gnss_protocols::UnicoreSatelliteConstellation::kGps &&
                 record.satellites[0].satellite_id == 2u &&
                 record.satellites[0].signal_mask == 0x13u &&
                 record.satellites[0].used_in_solution &&
                 record.satellites[0].common_view,
             "BESTSATA should decode documented GPS identity and signal-mask usage flags");
  ctx.Expect(record.satellites[1].constellation ==
                 universal_gnss_protocols::UnicoreSatelliteConstellation::kGlonass &&
                 record.satellites[1].satellite_id == 2u &&
                 record.satellites[1].glonass_frequency_channel == std::optional<std::int16_t>(-4) &&
                 !record.satellites[1].used_in_solution &&
                 record.satellites[1].common_view,
             "BESTSATA should preserve the documented GLONASS slot and frequency-channel suffix");
  ctx.Expect(record.satellites[2].constellation ==
                 universal_gnss_protocols::UnicoreSatelliteConstellation::kGalileo &&
                 record.satellites[2].used_in_solution &&
                 !record.satellites[2].common_view &&
                 record.satellites[3].constellation ==
                     universal_gnss_protocols::UnicoreSatelliteConstellation::kBeiDou &&
                 !record.satellites[3].used_in_solution,
             "BESTSATA should decode documented constellation names and conservative used flags");

  const auto state = universal_gnss_protocols::UnicoreBestSatToRuntimeState(record);
  ctx.Expect(universal_gnss::HasCapability(state, GnssCapability::kSatellitesTracked) &&
                 universal_gnss::HasCapability(state, GnssCapability::kSatellitesUsed),
             "BESTSATA runtime mapping should advertise only tracked and used satellites");
  ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kSatellitesTracked) &&
                 universal_gnss::HasValueAvailable(state, GnssCapability::kSatellitesUsed) &&
                 state.satellites_tracked == 4u &&
                 state.satellites_used == 2u,
             "BESTSATA runtime mapping should expose conservative tracked and used counts");
  ctx.Expect(!universal_gnss::HasCapability(state, GnssCapability::kSatellitesVisible) &&
                 !universal_gnss::HasCapability(state, GnssCapability::kMeanCn0) &&
                 !universal_gnss::HasCapability(state, GnssCapability::kMaxCn0) &&
                 !universal_gnss::HasCapability(state, GnssCapability::kRtkMode),
             "BESTSATA should not invent visibility, CN0, or RTK capabilities");
}

void TestRfAndHardwareParsingAndMapping(TestContext& ctx)
{
  const std::string jam_line =
      "#JAMSTATUSA,97,GPS,FINE,2190,365412000,0,0,18,14;SINGLE,120,2,0,0*e31418ea\r\n";
  const auto jam_result = ParseUnicoreJamStatus(BuildAsciiFrame(jam_line, 7777));
  ctx.Expect(jam_result.status == ParserStatus::kRecordReady && jam_result.record.has_value(),
             "valid JAMSTATUSA line should parse successfully");
  if (jam_result.record.has_value())
  {
    const auto& record = *jam_result.record;
    ctx.Expect(record.position_type == universal_gnss_protocols::UnicorePositionType::kSingle &&
                   record.cw_ratio == 120u &&
                   record.cw_state ==
                       universal_gnss_protocols::UnicoreJammingState::kStrongJamming,
               "JAMSTATUSA should parse the documented position type and CW jamming fields");

    const auto state = universal_gnss_protocols::UnicoreJamStatusToRuntimeState(record);
    ctx.Expect(universal_gnss::HasCapability(state, GnssCapability::kInterferenceState) &&
                   universal_gnss::HasCapability(state, GnssCapability::kJammingState) &&
                   universal_gnss::HasValueAvailable(state, GnssCapability::kInterferenceState) &&
                   universal_gnss::HasValueAvailable(state, GnssCapability::kJammingState) &&
                   state.interference_detected == std::optional<bool>(true) &&
                   state.jamming_detected == std::optional<bool>(true),
               "JAMSTATUSA runtime mapping should conservatively expose interference and jamming");
    ctx.Expect(state.fix_type == GnssFixType::kUnknown &&
                   !state.latitude_deg.has_value() &&
                   !state.horizontal_accuracy_m.has_value(),
               "JAMSTATUSA should not invent fix, position, or accuracy state");

    const auto event = universal_gnss_protocols::UnicoreJamStatusToDiagnosticEvent(record);
    ctx.Expect(event.severity == universal_gnss::GnssDiagnosticSeverity::kError &&
                   event.code == "unicore_jam_status.strong",
               "JAMSTATUSA diagnostic mapping should treat strong jamming as an error");
  }

  const std::string freq_jam_line =
      "#FREQJAMSTATUSA,97,GPS,FINE,2164,559464000,0,0,18,8;SINGLE,255,2,0,0,12,1,0,0*b0cdc7de\r\n";
  const auto freq_jam_result = ParseUnicoreFreqJamStatus(BuildAsciiFrame(freq_jam_line, 7778));
  ctx.Expect(freq_jam_result.status == ParserStatus::kRecordReady &&
                 freq_jam_result.record.has_value(),
             "valid FREQJAMSTATUSA line should parse successfully");
  if (freq_jam_result.record.has_value())
  {
    const auto& record = *freq_jam_result.record;
    ctx.Expect(record.l1.cw_ratio == 255u &&
                   record.l1.cw_state ==
                       universal_gnss_protocols::UnicoreJammingState::kStrongJamming &&
                   record.l2.cw_state ==
                       universal_gnss_protocols::UnicoreJammingState::kNone &&
                   record.l5.cw_state ==
                       universal_gnss_protocols::UnicoreJammingState::kJamming,
               "FREQJAMSTATUSA should parse documented per-frequency jamming fields");

    const auto state = universal_gnss_protocols::UnicoreFreqJamStatusToRuntimeState(record);
    ctx.Expect(universal_gnss::HasValueAvailable(state, GnssCapability::kInterferenceState) &&
                   universal_gnss::HasValueAvailable(state, GnssCapability::kJammingState) &&
                   state.interference_detected == std::optional<bool>(true) &&
                   state.jamming_detected == std::optional<bool>(true),
               "FREQJAMSTATUSA runtime mapping should set interference/jamming when any band is jammed");

    const auto event = universal_gnss_protocols::UnicoreFreqJamStatusToDiagnosticEvent(record);
    ctx.Expect(event.severity == universal_gnss::GnssDiagnosticSeverity::kError &&
                   event.code == "unicore_freq_jam_status.strong" &&
                   event.message.find("L1") != std::string::npos,
               "FREQJAMSTATUSA diagnostics should surface strong per-band jamming");
  }

  const std::string hw_status_line =
      "#HWSTATUSA,97,GPS,FINE,2221,111183000,0,0,18,15;66807,0.920,1.020,0.908,0,0.693,0.0,0x00,0,0x0377,0,0*9d7ce51d\r\n";
  const auto hw_status_result = ParseUnicoreHwStatus(BuildAsciiFrame(hw_status_line, 7779));
  ctx.Expect(hw_status_result.status == ParserStatus::kRecordReady &&
                 hw_status_result.record.has_value(),
             "valid HWSTATUSA line should parse successfully");
  if (hw_status_result.record.has_value())
  {
    const auto& record = *hw_status_result.record;
    ctx.Expect(record.dc09_v == 0.92f &&
                   record.dc10_v == 1.02f &&
                   record.dc18_v == 0.908f &&
                   !record.clock_drift_valid &&
                   record.hw_flag == 0x00u &&
                   record.pll_lock == 0x0377u,
               "HWSTATUSA should parse documented voltage, clock, and hardware fields");

    const auto event = universal_gnss_protocols::UnicoreHwStatusToDiagnosticEvent(record);
    ctx.Expect(event.severity == universal_gnss::GnssDiagnosticSeverity::kWarning &&
                   event.code == "unicore_hw_status.clock_invalid",
               "HWSTATUSA should conservatively warn only on invalid documented clock status");
  }

  const std::string agc_line =
      "#AGCA,65,GPS,FINE,2190,375570000,0,0,18,37;44,46,63,-1,-1,41,1,0,-1,-1*634f1e4b\r\n";
  const auto agc_result = universal_gnss_protocols::ParseUnicoreAgc(BuildAsciiFrame(agc_line, 7780));
  ctx.Expect(agc_result.status == ParserStatus::kRecordReady && agc_result.record.has_value(),
             "valid AGCA line should parse successfully");
  if (agc_result.record.has_value())
  {
    const auto& record = *agc_result.record;
    ctx.Expect(record.ant1_l1 == std::optional<std::int16_t>(44) &&
                   record.ant1_l2 == std::optional<std::int16_t>(46) &&
                   record.ant1_l5 == std::optional<std::int16_t>(63) &&
                   record.ant2_l1 == std::optional<std::int16_t>(41) &&
                   record.ant2_l2 == std::optional<std::int16_t>(1) &&
                   record.ant2_l5 == std::optional<std::int16_t>(0),
               "AGCA should preserve documented per-antenna AGC register values");
  }
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

  const std::string malformed_bestsat =
      "#BESTSATA,79,GPS,FINE,2203,226245800,0,0,18,22;"
      "1,GPS,2,GOOD,zzzzzzzz*12345678\r\n";
  const auto malformed_bestsat_result = ParseUnicoreBestSat(BuildAsciiFrame(malformed_bestsat));
  ctx.Expect(malformed_bestsat_result.status == ParserStatus::kInvalidData,
             "BESTSATA should reject malformed hexadecimal signal masks");

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

  const std::string malformed_jam =
      "#JAMSTATUSA,97,GPS,FINE,2190,365412000,0,0,18,14;SINGLE,not_a_ratio,2,0,0*e31418ea\r\n";
  const auto malformed_jam_result = ParseUnicoreJamStatus(BuildAsciiFrame(malformed_jam));
  ctx.Expect(malformed_jam_result.status == ParserStatus::kInvalidData,
             "JAMSTATUSA should reject malformed numeric essentials");

  const std::string malformed_hw =
      "#HWSTATUSA,97,GPS,FINE,2221,111183000,0,0,18,15;66807,0.920,1.020,0.908,2,0.693,0.0,0x00,0,0x0377,0,0*9d7ce51d\r\n";
  const auto malformed_hw_result = ParseUnicoreHwStatus(BuildAsciiFrame(malformed_hw));
  ctx.Expect(malformed_hw_result.status == ParserStatus::kInvalidData,
             "HWSTATUSA should reject unsupported clock validity values");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestPvtslnParsingAndRuntimeMapping(ctx);
  TestBestNavParsingAndMapping(ctx);
  TestBestNavCorrectionStateSemantics(ctx);
  TestRtkStatusAndRtcmStatusParsing(ctx);
  TestSatsInfoParsingAndRuntimeMapping(ctx);
  TestBestSatParsingAndRuntimeMapping(ctx);
  TestRfAndHardwareParsingAndMapping(ctx);
  TestMalformedAndMissingFields(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols Unicore ASCII parser tests passed\n";
  return EXIT_SUCCESS;
}
