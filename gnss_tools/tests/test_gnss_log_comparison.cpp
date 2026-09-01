#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "testdata_utils.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_tools/gnss_log_comparison.hpp"

namespace
{

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

  void ExpectNear(const double actual, const double expected, const double tolerance,
                  const std::string& message)
  {
    Expect(std::fabs(actual - expected) <= tolerance, message);
  }
};

universal_gnss_tools::GnssQualityReport MakeReport()
{
  universal_gnss_tools::GnssQualityReport report;
  report.summary.quality_level = universal_gnss_tools::GnssQualityLevel::kGood;
  report.summary.final_fix_type = universal_gnss::GnssFixType::kFix;
  report.summary.final_rtk_mode = universal_gnss::GnssRtkMode::kNone;
  report.summary.total_bytes_read = 100u;
  report.summary.records_processed = 5u;
  report.summary.runtime_updates = 3u;
  report.final_state.fix_type = universal_gnss::GnssFixType::kFix;
  report.final_state.rtk_mode = universal_gnss::GnssRtkMode::kNone;
  report.final_state.latitude_deg = 0.0;
  report.final_state.longitude_deg = 0.0;
  report.final_state.altitude_m = 100.0;
  report.final_state.horizontal_accuracy_m = 1.5f;
  report.final_state.satellites_used = 10u;
  report.final_state.mean_cn0_db_hz = 40.0f;
  report.final_state.correction_age_s = 2.0f;
  return report;
}

void TestComparisonUsesFinalNormalizedState(TestContext& ctx)
{
  const auto left = MakeReport();
  auto right = MakeReport();
  right.summary.quality_level = universal_gnss_tools::GnssQualityLevel::kRtkFixed;
  right.summary.final_fix_type = universal_gnss::GnssFixType::kRtkFixed;
  right.summary.final_rtk_mode = universal_gnss::GnssRtkMode::kFixed;
  right.final_state.fix_type = universal_gnss::GnssFixType::kRtkFixed;
  right.final_state.rtk_mode = universal_gnss::GnssRtkMode::kFixed;
  right.final_state.longitude_deg = 0.001;
  right.final_state.altitude_m = 103.5;
  right.final_state.horizontal_accuracy_m = 0.5f;
  right.final_state.satellites_used = 12u;
  right.final_state.mean_cn0_db_hz = 42.0f;
  right.final_state.correction_age_s = 1.0f;

  const auto comparison = universal_gnss_tools::CompareGnssQualityReports(left, right);
  ctx.Expect(!comparison.summary.final_fix_type_matches &&
                 !comparison.summary.final_rtk_mode_matches,
             "comparison should identify differing final categorical state");
  ctx.Expect(comparison.summary.final_horizontal_separation_m.has_value(),
             "comparison should calculate separation when both final positions exist");
  if (comparison.summary.final_horizontal_separation_m.has_value())
  {
    ctx.ExpectNear(*comparison.summary.final_horizontal_separation_m,
                   111.19508,
                   0.01,
                   "comparison should use a stable mean-earth horizontal separation");
  }
  ctx.Expect(comparison.summary.final_altitude_delta_m == std::optional<double>(3.5) &&
                 comparison.summary.horizontal_accuracy_delta_m == std::optional<double>(-1.0) &&
                 comparison.summary.satellites_used_delta == std::optional<std::int32_t>(2) &&
                 comparison.summary.mean_cn0_delta_db_hz == std::optional<double>(2.0) &&
                 comparison.summary.correction_age_delta_s == std::optional<double>(-1.0),
             "comparison deltas should be explicitly right-minus-left");
}

void TestMissingValuesRemainUnavailable(TestContext& ctx)
{
  auto left = MakeReport();
  auto right = MakeReport();
  left.final_state.latitude_deg.reset();
  right.final_state.longitude_deg.reset();
  left.final_state.altitude_m.reset();
  right.final_state.horizontal_accuracy_m.reset();
  left.final_state.satellites_used.reset();
  right.final_state.mean_cn0_db_hz.reset();
  left.final_state.correction_age_s.reset();

  const auto comparison = universal_gnss_tools::CompareGnssQualityReports(left, right);
  ctx.Expect(!comparison.summary.final_horizontal_separation_m.has_value() &&
                 !comparison.summary.final_altitude_delta_m.has_value() &&
                 !comparison.summary.horizontal_accuracy_delta_m.has_value() &&
                 !comparison.summary.satellites_used_delta.has_value() &&
                 !comparison.summary.mean_cn0_delta_db_hz.has_value() &&
                 !comparison.summary.correction_age_delta_s.has_value(),
             "comparison should preserve unavailable fields instead of inventing zero deltas");

  const std::string json = universal_gnss_tools::FormatGnssLogComparisonJson(comparison);
  ctx.Expect(json.find("{\"schema_version\":1,") == 0u &&
                 json.find("\"final_horizontal_separation_m\":null") != std::string::npos,
             "comparison JSON v1 should mark unavailable comparisons as null");
}

void TestSameReplayHasZeroSeparation(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("nmea/basic_fix.nmea");
  const auto comparison = universal_gnss_tools::BuildGnssLogComparisonBytes(bytes, bytes);
  ctx.Expect(comparison.summary.final_fix_type_matches &&
                 comparison.summary.final_rtk_mode_matches &&
                 comparison.summary.final_horizontal_separation_m == std::optional<double>(0.0),
             "the same replay input should compare to an identical final state and position");

  const std::string text = universal_gnss_tools::FormatGnssLogComparisonText(comparison);
  ctx.Expect(text.find("comparison schema_version=1") == 0u &&
                 text.find("final_horizontal_separation_m=0.000000") != std::string::npos,
             "comparison text should use the stable v1 report and deterministic numeric format");
}

void TestSanitizedReceiverPair(TestContext& ctx)
{
  const auto left = universal_gnss_tools::test::ReadBinaryFile("comparison/receiver_a.nmea");
  const auto right = universal_gnss_tools::test::ReadBinaryFile("comparison/receiver_b.nmea");
  const auto comparison = universal_gnss_tools::BuildGnssLogComparisonBytes(left, right);

  ctx.Expect(comparison.summary.final_horizontal_separation_m.has_value(),
             "the sanitized receiver pair should provide two final positions");
  if (comparison.summary.final_horizontal_separation_m.has_value())
  {
    ctx.ExpectNear(*comparison.summary.final_horizontal_separation_m,
                   111.19508,
                   0.01,
                   "the sanitized receiver pair should preserve its documented separation");
  }
  ctx.Expect(comparison.summary.final_altitude_delta_m == std::optional<double>(3.5) &&
                 comparison.summary.satellites_used_delta == std::optional<std::int32_t>(2),
             "the sanitized receiver pair should preserve its documented scalar deltas");
  ctx.Expect(comparison.summary.horizontal_accuracy_delta_m.has_value(),
             "the sanitized receiver pair should provide a horizontal accuracy delta");
  if (comparison.summary.horizontal_accuracy_delta_m.has_value())
  {
    ctx.ExpectNear(*comparison.summary.horizontal_accuracy_delta_m,
                   -0.2,
                   0.000001,
                   "the sanitized receiver pair should preserve its GST accuracy delta");
  }
}

}  // namespace

int main()
{
  TestContext ctx;
  TestComparisonUsesFinalNormalizedState(ctx);
  TestMissingValuesRemainUnavailable(ctx);
  TestSameReplayHasZeroSeparation(ctx);
  TestSanitizedReceiverPair(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools log comparison tests passed\n";
  return EXIT_SUCCESS;
}
