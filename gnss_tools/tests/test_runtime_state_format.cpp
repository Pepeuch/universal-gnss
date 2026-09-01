#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_tools/runtime_state_format.hpp"

namespace
{

using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::GnssRuntimeState;
using universal_gnss_driver::ReceiverSessionKind;

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

void TestCompactFormatting(TestContext& ctx)
{
  GnssRuntimeState state;
  state.timestamp_ns = 1234;
  state.fix_valid = true;
  state.fix_type = GnssFixType::kRtkFixed;
  state.rtk_mode = GnssRtkMode::kFixed;
  state.latitude_deg = 48.1234567891;
  state.longitude_deg = 2.2345678912;
  state.horizontal_accuracy_m = 0.25f;
  state.satellites_used = 18u;
  state.mean_cn0_db_hz = 35.5f;
  state.speed_over_ground_m_s = 2.5f;
  state.course_over_ground_deg = 54.7f;
  state.dual_antenna_heading = true;
  state.dual_antenna_baseline = true;
  state.baseline_azimuth_deg = 182.25f;
  state.baseline_pitch_deg = 0.1f;
  state.baseline_length_m = 1.5f;
  state.baseline_solution_status = universal_gnss::GnssBaselineSolutionStatus::kComputed;
  state.interference_detected = false;

  const std::string formatted =
      universal_gnss_tools::FormatRuntimeStateCompact(state, ReceiverSessionKind::kUblox);
  ctx.Expect(formatted.find("session=ublox") != std::string::npos,
             "compact formatting should include the selected receiver session");
  ctx.Expect(formatted.find("fix_type=rtk_fixed") != std::string::npos,
             "compact formatting should include the fix type");
  ctx.Expect(formatted.find("lat_deg=48.123456789") != std::string::npos &&
                 formatted.find("lon_deg=2.234567891") != std::string::npos,
             "compact formatting should preserve nine decimal places for coordinates");
  ctx.Expect(formatted.find("interference=false") != std::string::npos,
             "compact formatting should include optional booleans when available");
  ctx.Expect(formatted.find("speed_over_ground_m_s=2.500") != std::string::npos &&
                 formatted.find("course_over_ground_deg=54.70") != std::string::npos,
             "compact formatting should expose distinct ground speed and course");
  ctx.Expect(formatted.find("dual_antenna_heading=true") != std::string::npos &&
                 formatted.find("dual_antenna_baseline=true") != std::string::npos &&
                 formatted.find("baseline_azimuth_deg=182.25") != std::string::npos &&
                 formatted.find("baseline_solution_status=computed") != std::string::npos,
             "compact formatting should include compatibility and additive baseline fields");
}

void TestJsonFormatting(TestContext& ctx)
{
  GnssRuntimeState state;
  state.fix_valid = false;
  state.fix_type = GnssFixType::kUnknown;
  state.latitude_deg = 40.0789588272;
  state.longitude_deg = 116.2365102982;
  state.satellites_visible = 24u;
  state.speed_over_ground_m_s = 0.0f;
  state.course_over_ground_deg = 0.0f;
  state.dual_antenna_baseline = true;
  state.baseline_azimuth_deg = 182.25f;
  state.baseline_solution_status = universal_gnss::GnssBaselineSolutionStatus::kComputed;
  state.jamming_detected = true;
  state.utc_date = universal_gnss::GnssUtcDate{2002u, 7u, 4u};
  state.utc_time = universal_gnss::GnssUtcTime{20u, 15u, 30u, 0};

  const std::string formatted = universal_gnss_tools::FormatRuntimeStateJson(state, std::nullopt);
  ctx.Expect(formatted.find("\"selected_session\":null") != std::string::npos,
             "json formatting should emit null when no receiver session is selected");
  ctx.Expect(formatted.find("\"fix_valid\":false") != std::string::npos,
             "json formatting should include fix validity");
  ctx.Expect(formatted.find("\"latitude_deg\":40.078958827") != std::string::npos &&
                 formatted.find("\"longitude_deg\":116.236510298") != std::string::npos,
             "json formatting should preserve at least nine decimal places for coordinates");
  ctx.Expect(formatted.find("\"satellites_visible\":24") != std::string::npos,
             "json formatting should include available satellite counters");
  ctx.Expect(formatted.find("\"speed_over_ground_m_s\":0") != std::string::npos &&
                 formatted.find("\"course_over_ground_deg\":0") != std::string::npos,
             "json formatting should preserve valid zero speed and course values");
  ctx.Expect(formatted.find("\"dual_antenna_baseline\":true") != std::string::npos &&
                 formatted.find("\"baseline_azimuth_deg\":182.25") != std::string::npos &&
                 formatted.find("\"baseline_solution_status\":\"computed\"") !=
                     std::string::npos,
             "json formatting should include additive baseline fields");
  ctx.Expect(formatted.find("\"jamming_detected\":true") != std::string::npos,
             "json formatting should include available boolean fields");
  ctx.Expect(formatted.find("\"utc_date\":{\"year\":2002,\"month\":7,\"day\":4}") != std::string::npos &&
                 formatted.find("\"utc_time\":{\"hour\":20,\"minute\":15,\"second\":30,\"nanosecond\":0}") != std::string::npos,
             "json formatting should expose receiver UTC components independently");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestCompactFormatting(ctx);
  TestJsonFormatting(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools runtime state format tests passed\n";
  return EXIT_SUCCESS;
}
