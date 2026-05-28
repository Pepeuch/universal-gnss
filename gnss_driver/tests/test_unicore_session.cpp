#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/unicore_session.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::HasCapability;
using universal_gnss::HasValueAvailable;
using universal_gnss_driver::UnicoreSession;

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

constexpr const char* kBestNavLine =
    "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
    "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
    "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
    "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123*c1b4f7fe\r\n";

constexpr const char* kPvtslnLine =
    "#PVTSLNA,97,GPS,FINE,2190,364536000,0,0,18,13;"
    "NARROW_INT,60.5060,40.07898130522,116.23663134427,0.2000,0.1500,0.1800,0.9000,"
    "SINGLE,60.5060,40.07898130522,116.23663134427,4.3353,46,28,46,28,0.0009,-0.0031,-0.0032,"
    "SOL_COMPUTED,1.5000,182.2500,0.1000,28,25,12,8,2.1753,1.3480,0.6840,1.8392,1.7072,5.0,"
    "28,25,26*1e33c8cb\r\n";

constexpr const char* kRtkStatusLine =
    "#RTKSTATUSA,97,GPS,FINE,2190,365354000,0,0,18,1;"
    "0,0,0,0,0,0,0,0,0,0,0,NARROW_INT,5,0,1,12,0*f06a8a06\r\n";

constexpr const char* kSatsInfoLine =
    "#SATSINFOA,96,GPS,FINE,2215,367199000,0,0,18,16;"
    "3,2,0,0,0,63,"
    "2,302,51,0,45,0,2,0,42,9,2,"
    "4,48,17,0,37,0,3,0,43,14,3,0,39,9,3,"
    "5,225,14,1,50,0,1*abcdef12\r\n";

void TestBestNavUpdatesRuntimeState(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kBestNavLine, 2222);

  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.bytes_seen == std::string(kBestNavLine).size(),
             "BESTNAVA feed should count input bytes");
  ctx.Expect(metrics.lines_seen == 1u && metrics.ascii_records_seen == 1u &&
                 metrics.records_parsed == 1u && metrics.runtime_updates == 1u,
             "BESTNAVA feed should count one parsed runtime update");
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(2222) &&
                 state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFloat &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFloat),
             "BESTNAVA should update fix and RTK state");
  ctx.Expect(state.latitude_deg == std::optional<double>(40.0789588272) &&
                 state.longitude_deg == std::optional<double>(116.2365102982) &&
                 state.altitude_m == std::optional<double>(65.8312),
             "BESTNAVA should update coordinates and altitude");
}

void TestPvtslnUpdatesHeading(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kPvtslnLine, 1111);

  const auto& state = session.current_state();
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFixed &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed),
             "PVTSLNA should expose RTK fixed state");
  ctx.Expect(HasCapability(state, GnssCapability::kHeading) &&
                 HasValueAvailable(state, GnssCapability::kHeading) &&
                 state.heading_deg == std::optional<double>(182.25),
             "PVTSLNA should update heading when the message reports a computed heading");
}

void TestRtkStatusUpdatesDualAntenna(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kRtkStatusLine, 3333);

  const auto& state = session.current_state();
  ctx.Expect(state.fix_valid &&
                 state.fix_type == GnssFixType::kRtkFixed &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed),
             "RTKSTATUSA should update RTK fixed state");
  ctx.Expect(HasCapability(state, GnssCapability::kDualAntennaHeading) &&
                 HasValueAvailable(state, GnssCapability::kDualAntennaHeading) &&
                 state.dual_antenna_heading == std::optional<bool>(true),
             "RTKSTATUSA should update dual-antenna heading state");
}

void TestSatsInfoUpdatesTrackedAndCn0(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString(kSatsInfoLine, 5555);

  const auto& state = session.current_state();
  ctx.Expect(HasCapability(state, GnssCapability::kSatellitesTracked) &&
                 HasValueAvailable(state, GnssCapability::kSatellitesTracked) &&
                 state.satellites_tracked == std::optional<std::uint16_t>(3u),
             "SATSINFOA should update tracked-satellite count");
  ctx.Expect(HasCapability(state, GnssCapability::kMeanCn0) &&
                 HasCapability(state, GnssCapability::kMaxCn0) &&
                 HasValueAvailable(state, GnssCapability::kMeanCn0) &&
                 HasValueAvailable(state, GnssCapability::kMaxCn0) &&
                 state.mean_cn0_db_hz == std::optional<float>(46.0f) &&
                 state.max_cn0_db_hz == std::optional<float>(50.0f),
             "SATSINFOA should update CN0 summaries");
}

void TestUnknownAndMalformedRecords(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString("#FOOBARA,97,GPS,FINE,1,2,0,0,0,0;payload\r\n");
  session.FeedString(
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,SINGLE,not_a_latitude,116.2365102982,65.8312*abcd1234\r\n");

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.lines_seen == 2u && metrics.ascii_records_seen == 2u,
             "unknown and malformed lines should still count as seen records");
  ctx.Expect(metrics.unknown_records == 1u && metrics.records_rejected == 1u &&
                 metrics.records_parsed == 0u && metrics.runtime_updates == 0u,
             "unknown and malformed records should be counted separately");
  ctx.Expect(session.current_state().fix_type == GnssFixType::kUnknown,
             "unknown and malformed records should not invent runtime state");
}

void TestPartialChunksAcrossFeeds(TestContext& ctx)
{
  UnicoreSession session;
  const std::string line = kBestNavLine;
  session.FeedString(std::string_view(line.data(), 32u), 7000);
  session.FeedString(std::string_view(line.data() + 32u, line.size() - 32u), 7001);

  const auto& metrics = session.metrics();
  ctx.Expect(metrics.lines_seen == 1u && metrics.records_parsed == 1u &&
                 metrics.runtime_updates == 1u,
             "split BESTNAVA input should parse after the final chunk arrives");
  ctx.Expect(session.current_state().timestamp_ns == std::optional<std::int64_t>(7000),
             "split input should preserve the first-byte timestamp of the framed record");
}

void TestFinalizeAndReset(TestContext& ctx)
{
  UnicoreSession session;
  session.FeedString("#BESTNAVA,97,GPS,FINE,2294");
  session.Finalize();

  ctx.Expect(session.metrics().malformed_lines == 1u,
             "finalizing a truncated trailing line should count a malformed line");

  session.FeedString(kBestNavLine, 8000);
  session.Reset();
  const auto& metrics = session.metrics();
  const auto& state = session.current_state();
  ctx.Expect(metrics.bytes_seen == 0u && metrics.lines_seen == 0u &&
                 metrics.records_parsed == 0u && metrics.runtime_updates == 0u &&
                 metrics.malformed_lines == 0u,
             "reset should clear session metrics");
  ctx.Expect(state.fix_type == GnssFixType::kUnknown && !state.fix_valid &&
                 !state.latitude_deg.has_value(),
             "reset should clear the aggregated runtime state");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestBestNavUpdatesRuntimeState(ctx);
  TestPvtslnUpdatesHeading(ctx);
  TestRtkStatusUpdatesDualAntenna(ctx);
  TestSatsInfoUpdatesTrackedAndCn0(ctx);
  TestUnknownAndMalformedRecords(ctx);
  TestPartialChunksAcrossFeeds(ctx);
  TestFinalizeAndReset(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver Unicore session tests passed\n";
  return EXIT_SUCCESS;
}
