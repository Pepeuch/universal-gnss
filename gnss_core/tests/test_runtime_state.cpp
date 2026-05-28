#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>

#include "universal_gnss/gnss_runtime_state.hpp"

namespace
{

using universal_gnss::ComputeValueFlagsFromFields;
using universal_gnss::GnssCapability;
using universal_gnss::GnssCapabilityFlags;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::GnssRuntimeState;
using universal_gnss::HasCapability;
using universal_gnss::HasValidCapabilityValueInvariant;
using universal_gnss::HasValueAvailable;
using universal_gnss::RefreshValueFlagsFromFields;
using universal_gnss::SetCapability;
using universal_gnss::SetOptionalValue;
using universal_gnss::SetValueAvailable;
using universal_gnss::ToFlag;

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

void TestCapabilityValueInvariant(TestContext& ctx)
{
  GnssRuntimeState state;
  SetCapability(state, GnssCapability::kHdop);
  ctx.Expect(SetValueAvailable(state, GnssCapability::kHdop),
             "value flag should be set when the capability exists");
  ctx.Expect(HasValidCapabilityValueInvariant(state),
             "matching capability/value bits should satisfy the invariant");

  state.value_flags = static_cast<GnssCapabilityFlags>(
      ToFlag(GnssCapability::kHdop) | ToFlag(GnssCapability::kVdop));
  ctx.Expect(!HasValidCapabilityValueInvariant(state),
             "value flags must not contain bits that are absent from capability flags");
}

void TestSettingValueWithoutCapabilityIsRejected(TestContext& ctx)
{
  GnssRuntimeState state;

  ctx.Expect(!SetOptionalValue(state, GnssCapability::kHdop, state.hdop, 0.8f),
             "setting a value without its capability should be rejected");
  ctx.Expect(!state.hdop.has_value(), "rejected value assignment should leave the field empty");
  ctx.Expect(!HasValueAvailable(state, GnssCapability::kHdop),
             "rejected value assignment should not set value flags");

  state.hdop = 0.8f;
  RefreshValueFlagsFromFields(state);
  ctx.Expect(!HasValueAvailable(state, GnssCapability::kHdop),
             "refresh should not invent a value flag when capability is missing");
  ctx.Expect(HasValidCapabilityValueInvariant(state),
             "refresh should preserve the capability/value invariant");
}

void TestMinimalStateDoesNotInventRichFields(TestContext& ctx)
{
  GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = GnssFixType::kFix;
  state.latitude_deg = 48.123456;
  state.longitude_deg = 2.345678;
  state.altitude_m = 123.4;

  RefreshValueFlagsFromFields(state);

  ctx.Expect(!state.rtk_mode.has_value(), "minimal state should not invent RTK mode");
  ctx.Expect(!state.horizontal_accuracy_m.has_value(),
             "minimal state should not invent horizontal accuracy");
  ctx.Expect(!state.satellites_used.has_value(),
             "minimal state should not invent satellite counts");
  ctx.Expect(!state.correction_age_s.has_value(),
             "minimal state should not invent correction age");
  ctx.Expect(state.capability_flags == 0u, "minimal state should not declare rich capabilities");
  ctx.Expect(state.value_flags == 0u, "minimal state should not declare rich values");
}

void TestRicherRtkStateExposesExpectedFlags(TestContext& ctx)
{
  GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = GnssFixType::kRtkFixed;
  state.latitude_deg = 48.0;
  state.longitude_deg = 2.0;
  state.altitude_m = 100.0;

  SetCapability(state, GnssCapability::kRtkMode);
  SetCapability(state, GnssCapability::kHorizontalAccuracy);
  SetCapability(state, GnssCapability::kSatellitesUsed);
  SetCapability(state, GnssCapability::kCorrectionAge);
  SetCapability(state, GnssCapability::kMeanCn0);
  SetCapability(state, GnssCapability::kDualAntennaHeading);
  SetCapability(state, GnssCapability::kJammingState);

  ctx.Expect(SetOptionalValue(state, GnssCapability::kRtkMode, state.rtk_mode, GnssRtkMode::kFixed),
             "rtk_mode should be assignable when capability exists");
  ctx.Expect(SetOptionalValue(
                 state, GnssCapability::kHorizontalAccuracy, state.horizontal_accuracy_m, 0.02f),
             "horizontal_accuracy_m should be assignable when capability exists");
  ctx.Expect(SetOptionalValue(state, GnssCapability::kSatellitesUsed, state.satellites_used, 18u),
             "satellites_used should be assignable when capability exists");
  ctx.Expect(SetOptionalValue(state, GnssCapability::kCorrectionAge, state.correction_age_s, 0.4f),
             "correction_age_s should be assignable when capability exists");
  ctx.Expect(SetOptionalValue(state, GnssCapability::kMeanCn0, state.mean_cn0_db_hz, 41.5f),
             "mean_cn0_db_hz should be assignable when capability exists");
  ctx.Expect(SetOptionalValue(
                 state, GnssCapability::kDualAntennaHeading, state.dual_antenna_heading, true),
             "dual_antenna_heading should be assignable when capability exists");
  ctx.Expect(SetOptionalValue(state, GnssCapability::kJammingState, state.jamming_detected, false),
             "jamming_detected should be assignable when capability exists");

  ctx.Expect(HasCapability(state, GnssCapability::kRtkMode),
             "capability_flags should retain declared RTK mode support");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kRtkMode),
             "value_flags should expose RTK mode availability");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kHorizontalAccuracy),
             "value_flags should expose horizontal accuracy availability");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kSatellitesUsed),
             "value_flags should expose satellites_used availability");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kCorrectionAge),
             "value_flags should expose correction age availability");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kMeanCn0),
             "value_flags should expose mean CN0 availability");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kDualAntennaHeading),
             "value_flags should expose dual antenna state availability");
  ctx.Expect(HasValueAvailable(state, GnssCapability::kJammingState),
             "value_flags should expose jamming state availability");
  ctx.Expect(HasValidCapabilityValueInvariant(state),
             "richer RTK state should preserve the capability/value invariant");
}

void TestCapabilityBitsFitInUint32(TestContext& ctx)
{
  ctx.Expect((std::is_same<std::underlying_type<GnssCapability>::type, std::uint32_t>::value),
             "GnssCapability bits must fit in uint32_t");
  ctx.Expect(sizeof(GnssCapabilityFlags) == sizeof(std::uint32_t),
             "GnssCapabilityFlags must fit in uint32_t");
  ctx.Expect(ToFlag(GnssCapability::kJammingState) != 0u,
             "highest currently defined capability bit should be non-zero");
}

void TestDefaultRuntimeStateIsSafeUnknown(TestContext& ctx)
{
  GnssRuntimeState state;

  ctx.Expect(!state.timestamp_ns.has_value(), "default state should not invent a timestamp");
  ctx.Expect(!state.fix_valid, "default state should not claim a valid fix");
  ctx.Expect(state.fix_type == GnssFixType::kUnknown,
             "default state should begin with an unknown fix type");
  ctx.Expect(!state.rtk_mode.has_value(), "default state should not invent RTK mode");
  ctx.Expect(!state.latitude_deg.has_value(), "default state should not invent latitude");
  ctx.Expect(state.capability_flags == 0u, "default state should not declare any capabilities");
  ctx.Expect(state.value_flags == 0u, "default state should not declare any values");
  ctx.Expect(ComputeValueFlagsFromFields(state) == 0u,
             "default state should compute zero value flags");
  ctx.Expect(HasValidCapabilityValueInvariant(state),
             "default state should satisfy the capability/value invariant");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestCapabilityValueInvariant(ctx);
  TestSettingValueWithoutCapabilityIsRejected(ctx);
  TestMinimalStateDoesNotInventRichFields(ctx);
  TestRicherRtkStateExposesExpectedFlags(ctx);
  TestCapabilityBitsFitInUint32(ctx);
  TestDefaultRuntimeStateIsSafeUnknown(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_core runtime state tests passed\n";
  return EXIT_SUCCESS;
}
