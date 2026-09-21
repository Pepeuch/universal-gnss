#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss_driver/position_payload_freshness.hpp"

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
};

universal_gnss::GnssRuntimeState MakeFix(const std::int64_t timestamp_ns,
                                         const double latitude_deg = 48.0,
                                         const double longitude_deg = 2.0)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = timestamp_ns;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = latitude_deg;
  state.longitude_deg = longitude_deg;
  return state;
}

void TestFieldPatternAndRecovery(TestContext& ctx)
{
  universal_gnss_driver::PositionPayloadFreshnessTracker tracker;
  constexpr std::int64_t kSecondNs = 1000000000;

  for (std::uint32_t second = 0u; second <= 45u; ++second)
  {
    tracker.Observe(MakeFix(static_cast<std::int64_t>(second) * kSecondNs),
                    universal_gnss_driver::MakeReceiverWeekTowEpoch(2437u, second * 1000u));
  }

  const auto& frozen = tracker.metrics();
  ctx.Expect(frozen.valid_position_payload_observations == 46u,
             "each 1 Hz position solution should remain an observation");
  ctx.Expect(frozen.position_payload_changes == 1u &&
                 frozen.consecutive_identical_position_observations == 46u,
             "bit-identical coordinates should expose one value and a 46-observation run");
  ctx.Expect(frozen.unchanged_observation_span_ns == std::optional<std::int64_t>(45 * kSecondNs),
             "unchanged observation span should expose the 45-second field pattern");
  ctx.Expect(frozen.receiver_epoch_advances == 45u &&
                 frozen.last_receiver_epoch_relation ==
                     universal_gnss_driver::ReceiverEpochRelation::kAdvanced,
             "receiver epoch advancement must remain distinct from payload changes");

  tracker.Observe(MakeFix(46 * kSecondNs, 48.000000001, 2.0),
                  universal_gnss_driver::MakeReceiverWeekTowEpoch(2437u, 46000u));
  const auto& recovered = tracker.metrics();
  ctx.Expect(recovered.position_payload_changes == 2u &&
                 recovered.consecutive_identical_position_observations == 1u &&
                 recovered.unchanged_observation_span_ns == std::optional<std::int64_t>(0),
             "small coordinate movement should recover the unchanged-payload state immediately");
}

void TestStationaryAndInvalidObservations(TestContext& ctx)
{
  universal_gnss_driver::PositionPayloadFreshnessTracker tracker;
  tracker.Observe(MakeFix(0));
  tracker.Observe(MakeFix(1000000000));

  ctx.Expect(tracker.metrics().consecutive_identical_position_observations == 2u,
             "a stationary receiver must be represented as unchanged, not rejected");

  auto no_fix = MakeFix(2000000000);
  no_fix.fix_valid = false;
  no_fix.fix_type = universal_gnss::GnssFixType::kNoFix;
  no_fix.latitude_deg.reset();
  no_fix.longitude_deg.reset();
  tracker.Observe(no_fix);
  ctx.Expect(tracker.metrics().invalid_position_observations == 1u &&
                 tracker.metrics().consecutive_identical_position_observations == 0u &&
                 !tracker.metrics().unchanged_observation_span_ns.has_value(),
             "invalid observations should break the comparable-position run");

  tracker.Observe(MakeFix(3000000000));
  ctx.Expect(tracker.metrics().position_payload_changes == 2u &&
                 tracker.metrics().consecutive_identical_position_observations == 1u,
             "fix recovery should establish a new baseline without spanning the invalid interval");
}

void TestReceiverEpochStatesAndReset(TestContext& ctx)
{
  universal_gnss_driver::PositionPayloadFreshnessTracker tracker;
  tracker.Observe(MakeFix(0), universal_gnss_driver::MakeGpsTowEpoch(1000u));
  tracker.Observe(MakeFix(1000000000), universal_gnss_driver::MakeGpsTowEpoch(1000u));
  tracker.Observe(MakeFix(2000000000), universal_gnss_driver::MakeGpsTowEpoch(900u));

  ctx.Expect(tracker.metrics().consecutive_identical_receiver_epochs == 1u &&
                 tracker.metrics().receiver_epoch_regressions == 1u &&
                 tracker.metrics().last_receiver_epoch_relation ==
                     universal_gnss_driver::ReceiverEpochRelation::kRegressed,
             "identical and regressed receiver epochs should be independently visible");

  tracker.Reset();
  ctx.Expect(tracker.metrics().valid_position_payload_observations == 0u &&
                 !tracker.metrics().last_receiver_epoch.has_value(),
             "source/session reset must clear all freshness history");
}

}  // namespace

int main()
{
  TestContext ctx;
  TestFieldPatternAndRecovery(ctx);
  TestStationaryAndInvalidObservations(ctx);
  TestReceiverEpochStatesAndReset(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All position payload freshness tests passed\n";
  return EXIT_SUCCESS;
}
