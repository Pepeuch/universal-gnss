#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_runtime_aggregator.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssBaselineSolutionStatus;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRuntimeAggregator;
using universal_gnss::GnssRuntimeState;
using universal_gnss::HasCapability;
using universal_gnss::HasValidCapabilityValueInvariant;
using universal_gnss::HasValueAvailable;
using universal_gnss::SetCapability;
using universal_gnss::SetOptionalValue;

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

GnssRuntimeState MakeGgaLikeState()
{
  GnssRuntimeState state;
  state.timestamp_ns = 100;
  state.fix_valid = true;
  state.fix_type = GnssFixType::kFix;
  state.latitude_deg = 48.1173;
  state.longitude_deg = 11.5166667;
  state.altitude_m = 545.4;
  return state;
}

GnssRuntimeState MakeGsaLikeState()
{
  GnssRuntimeState state;
  state.timestamp_ns = 110;
  state.fix_valid = true;
  state.fix_type = GnssFixType::kFix;
  SetCapability(state, GnssCapability::kHdop);
  SetCapability(state, GnssCapability::kVdop);
  SetCapability(state, GnssCapability::kSatellitesUsed);
  SetOptionalValue(state, GnssCapability::kHdop, state.hdop, 1.0f);
  SetOptionalValue(state, GnssCapability::kVdop, state.vdop, 1.5f);
  SetOptionalValue(state, GnssCapability::kSatellitesUsed, state.satellites_used, 8u);
  return state;
}

GnssRuntimeState MakeGsvLikeState()
{
  GnssRuntimeState state;
  state.timestamp_ns = 120;
  SetCapability(state, GnssCapability::kSatellitesVisible);
  SetCapability(state, GnssCapability::kMeanCn0);
  SetCapability(state, GnssCapability::kMaxCn0);
  SetOptionalValue(state, GnssCapability::kSatellitesVisible, state.satellites_visible, 8u);
  SetOptionalValue(state, GnssCapability::kMeanCn0, state.mean_cn0_db_hz, 41.25f);
  SetOptionalValue(state, GnssCapability::kMaxCn0, state.max_cn0_db_hz, 43.0f);
  return state;
}

void TestMergingPartialStatesBuildsCoherentRuntime(TestContext& ctx)
{
  GnssRuntimeAggregator aggregator;

  ctx.Expect(aggregator.Merge(MakeGgaLikeState()), "GGA-like update should merge");
  ctx.Expect(aggregator.Merge(MakeGsaLikeState()), "GSA-like update should merge");
  ctx.Expect(aggregator.Merge(MakeGsvLikeState()), "GSV-like update should merge");

  const GnssRuntimeState& state = aggregator.state();
  ctx.Expect(state.fix_valid && state.fix_type == GnssFixType::kFix,
             "aggregator should preserve the generic fix");
  ctx.Expect(state.latitude_deg == std::optional<double>(48.1173) &&
                 state.longitude_deg == std::optional<double>(11.5166667) &&
                 state.altitude_m == std::optional<double>(545.4),
             "aggregator should preserve GGA-like position fields");
  ctx.Expect(state.hdop == std::optional<float>(1.0f) &&
                 state.vdop == std::optional<float>(1.5f) &&
                 state.satellites_used == std::optional<std::uint16_t>(8u),
             "aggregator should merge GSA-like DOP and satellites-used fields");
  ctx.Expect(state.satellites_visible == std::optional<std::uint16_t>(8u) &&
                 state.mean_cn0_db_hz == std::optional<float>(41.25f) &&
                 state.max_cn0_db_hz == std::optional<float>(43.0f),
             "aggregator should merge GSV-like visibility and CN0 fields");
}

void TestExistingValuesSurviveUpdatesWithoutValueFlags(TestContext& ctx)
{
  GnssRuntimeAggregator aggregator;
  GnssRuntimeState initial;
  initial.timestamp_ns = 100;
  SetCapability(initial, GnssCapability::kHdop);
  SetOptionalValue(initial, GnssCapability::kHdop, initial.hdop, 0.9f);
  aggregator.Merge(initial);

  GnssRuntimeState update;
  update.timestamp_ns = 110;
  SetCapability(update, GnssCapability::kHdop);
  SetCapability(update, GnssCapability::kVdop);
  update.hdop = 9.9f;

  ctx.Expect(aggregator.Merge(update),
             "capability-only update should still be accepted as metadata");
  ctx.Expect(aggregator.state().hdop == std::optional<float>(0.9f),
             "missing value flags must not overwrite an existing optional value");
  ctx.Expect(HasValueAvailable(aggregator.state(), GnssCapability::kHdop),
             "existing value flag should survive a capability-only update");
  ctx.Expect(HasCapability(aggregator.state(), GnssCapability::kVdop),
             "new capability metadata should still be retained");
}

void TestNewestTimestampWins(TestContext& ctx)
{
  GnssRuntimeAggregator aggregator;

  GnssRuntimeState older;
  older.timestamp_ns = 100;
  older.latitude_deg = 48.0;
  aggregator.Merge(older);

  GnssRuntimeState stale_update;
  stale_update.timestamp_ns = 90;
  stale_update.latitude_deg = 49.0;
  aggregator.Merge(stale_update);
  ctx.Expect(aggregator.state().latitude_deg == std::optional<double>(48.0),
             "older timestamped updates must not replace newer latitude");

  GnssRuntimeState newer_update;
  newer_update.timestamp_ns = 110;
  newer_update.latitude_deg = 50.0;
  aggregator.Merge(newer_update);
  ctx.Expect(aggregator.state().latitude_deg == std::optional<double>(50.0),
             "newer timestamped updates should replace older latitude");
}

void TestLastUpdateWinsWithoutTimestamp(TestContext& ctx)
{
  GnssRuntimeAggregator aggregator;

  GnssRuntimeState first;
  first.latitude_deg = 10.0;
  aggregator.Merge(first);

  GnssRuntimeState second;
  second.latitude_deg = 11.0;
  aggregator.Merge(second);
  ctx.Expect(aggregator.state().latitude_deg == std::optional<double>(11.0),
             "without timestamps, the last update should win");

  GnssRuntimeState timed;
  timed.timestamp_ns = 50;
  timed.latitude_deg = 12.0;
  aggregator.Merge(timed);
  ctx.Expect(aggregator.state().latitude_deg == std::optional<double>(12.0),
             "timestamped updates should still apply after untimed ones");

  GnssRuntimeState untimed_after_timed;
  untimed_after_timed.latitude_deg = 13.0;
  aggregator.Merge(untimed_after_timed);
  ctx.Expect(aggregator.state().latitude_deg == std::optional<double>(13.0),
             "an untimed last update should win by arrival order");
}

void TestResetClearsState(TestContext& ctx)
{
  GnssRuntimeAggregator aggregator;
  aggregator.Merge(MakeGgaLikeState());
  aggregator.Merge(MakeGsaLikeState());
  aggregator.Reset();

  const GnssRuntimeState& state = aggregator.state();
  ctx.Expect(!state.timestamp_ns.has_value(), "reset should clear the aggregate timestamp");
  ctx.Expect(!state.fix_valid, "reset should clear the fix-valid state");
  ctx.Expect(state.fix_type == GnssFixType::kUnknown, "reset should restore unknown fix type");
  ctx.Expect(state.capability_flags == 0u && state.value_flags == 0u,
             "reset should clear capability and value flags");
}

void TestInvalidValueFlagsDoNotLeakIntoAggregate(TestContext& ctx)
{
  GnssRuntimeAggregator aggregator;
  GnssRuntimeState invalid;
  invalid.timestamp_ns = 100;
  invalid.hdop = 0.7f;
  invalid.value_flags = universal_gnss::ToFlag(GnssCapability::kHdop);

  aggregator.Merge(invalid);
  const GnssRuntimeState& state = aggregator.state();
  ctx.Expect(!state.hdop.has_value(), "value flags without capability must not populate fields");
  ctx.Expect(!HasValueAvailable(state, GnssCapability::kHdop),
             "aggregate value flags must not leak unsupported bits");
  ctx.Expect(HasValidCapabilityValueInvariant(state),
             "aggregator must preserve the capability/value invariant");
}

void TestNoInventedRtkOrRfFields(TestContext& ctx)
{
  GnssRuntimeAggregator aggregator;
  aggregator.Merge(MakeGgaLikeState());
  aggregator.Merge(MakeGsaLikeState());
  aggregator.Merge(MakeGsvLikeState());

  const GnssRuntimeState& state = aggregator.state();
  ctx.Expect(!HasCapability(state, GnssCapability::kRtkMode) && !state.rtk_mode.has_value(),
             "aggregator should not invent RTK mode");
  ctx.Expect(!HasCapability(state, GnssCapability::kInterferenceState) &&
                 !state.interference_detected.has_value(),
             "aggregator should not invent interference state");
  ctx.Expect(!HasCapability(state, GnssCapability::kJammingState) &&
                 !state.jamming_detected.has_value(),
             "aggregator should not invent jamming state");
  ctx.Expect(!HasCapability(state, GnssCapability::kBaselineAzimuth) &&
                 !state.baseline_azimuth_deg.has_value() &&
                 !HasCapability(state, GnssCapability::kDualAntennaBaseline) &&
                 !state.dual_antenna_baseline.has_value(),
             "aggregator should not invent antenna-baseline state");
}

void TestKnownFalseBooleanStateSurvivesAggregation(TestContext& ctx)
{
  GnssRuntimeAggregator aggregator;

  GnssRuntimeState update;
  update.timestamp_ns = 100;
  SetCapability(update, GnssCapability::kCorrectionsActive);
  SetOptionalValue(update, GnssCapability::kCorrectionsActive, update.corrections_active, false);

  ctx.Expect(aggregator.Merge(update), "known false boolean update should merge");
  ctx.Expect(HasCapability(aggregator.state(), GnssCapability::kCorrectionsActive),
             "aggregate should retain corrections-active capability");
  ctx.Expect(HasValueAvailable(aggregator.state(), GnssCapability::kCorrectionsActive),
             "aggregate should retain a known false corrections-active value");
  ctx.Expect(aggregator.state().corrections_active == std::optional<bool>(false),
             "aggregate should preserve false without treating it as unknown");
}

void TestBaselineFoundationFieldsMergeIndependently(TestContext& ctx)
{
  GnssRuntimeAggregator aggregator;

  GnssRuntimeState geometry;
  geometry.timestamp_ns = 100;
  SetCapability(geometry, GnssCapability::kHeading);
  SetCapability(geometry, GnssCapability::kBaselineAzimuth);
  SetCapability(geometry, GnssCapability::kBaselinePitch);
  SetCapability(geometry, GnssCapability::kBaselineLength);
  ctx.Expect(SetOptionalValue(geometry, GnssCapability::kHeading, geometry.heading_deg, 182.25f),
             "baseline geometry fixture should accept compatibility heading");
  ctx.Expect(SetOptionalValue(
                 geometry, GnssCapability::kBaselineAzimuth, geometry.baseline_azimuth_deg, 182.25f),
             "baseline geometry fixture should accept baseline azimuth");
  ctx.Expect(SetOptionalValue(
                 geometry, GnssCapability::kBaselinePitch, geometry.baseline_pitch_deg, 0.1f),
             "baseline geometry fixture should accept baseline pitch");
  ctx.Expect(SetOptionalValue(
                 geometry, GnssCapability::kBaselineLength, geometry.baseline_length_m, 1.5f),
             "baseline geometry fixture should accept baseline length");

  GnssRuntimeState status;
  status.timestamp_ns = 110;
  SetCapability(status, GnssCapability::kDualAntennaBaseline);
  SetCapability(status, GnssCapability::kBaselineSolutionStatus);
  ctx.Expect(SetOptionalValue(
                 status,
                 GnssCapability::kDualAntennaBaseline,
                 status.dual_antenna_baseline,
                 true),
             "baseline status fixture should accept boolean baseline state");
  ctx.Expect(SetOptionalValue(status,
                              GnssCapability::kBaselineSolutionStatus,
                              status.baseline_solution_status,
                              GnssBaselineSolutionStatus::kComputed),
             "baseline status fixture should accept solution status");

  ctx.Expect(aggregator.Merge(geometry), "baseline geometry update should merge");
  ctx.Expect(aggregator.Merge(status), "baseline status update should merge");

  const auto& state = aggregator.state();
  ctx.Expect(state.heading_deg == std::optional<float>(182.25f) &&
                 state.baseline_azimuth_deg == std::optional<float>(182.25f) &&
                 state.baseline_pitch_deg == std::optional<float>(0.1f) &&
                 state.baseline_length_m == std::optional<float>(1.5f),
             "aggregator should preserve additive baseline geometry fields");
  ctx.Expect(state.dual_antenna_baseline == std::optional<bool>(true) &&
                 state.baseline_solution_status ==
                     std::optional<GnssBaselineSolutionStatus>(
                         GnssBaselineSolutionStatus::kComputed),
             "aggregator should merge additive baseline status fields");
}

void TestAggregateTimestampTracksNewestKnownSample(TestContext& ctx)
{
  GnssRuntimeAggregator aggregator;
  aggregator.Merge(MakeGgaLikeState());
  aggregator.Merge(MakeGsaLikeState());
  aggregator.Merge(MakeGsvLikeState());

  ctx.Expect(aggregator.state().timestamp_ns == std::optional<std::int64_t>(120),
             "aggregate timestamp should track the newest accepted timestamped update");

  GnssRuntimeState untimed;
  untimed.longitude_deg = 12.0;
  aggregator.Merge(untimed);
  ctx.Expect(aggregator.state().timestamp_ns == std::optional<std::int64_t>(120),
             "untimed updates should not invent or clear the aggregate timestamp");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestMergingPartialStatesBuildsCoherentRuntime(ctx);
  TestExistingValuesSurviveUpdatesWithoutValueFlags(ctx);
  TestNewestTimestampWins(ctx);
  TestLastUpdateWinsWithoutTimestamp(ctx);
  TestResetClearsState(ctx);
  TestInvalidValueFlagsDoNotLeakIntoAggregate(ctx);
  TestNoInventedRtkOrRfFields(ctx);
  TestKnownFalseBooleanStateSurvivesAggregation(ctx);
  TestBaselineFoundationFieldsMergeIndependently(ctx);
  TestAggregateTimestampTracksNewestKnownSample(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_core runtime aggregator tests passed\n";
  return EXIT_SUCCESS;
}
