#include "universal_gnss_mavros/mavlink_gnss_adapter.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {

using universal_gnss::GnssCapability;
using universal_gnss_mavros::MavlinkGnssAdapter;
using universal_gnss_mavros::RawGpsObservation;
using universal_gnss_mavros::Receiver;
using universal_gnss_mavros::RtkObservation;
using universal_gnss_mavros::SystemTimeObservation;

int failures = 0;

#define EXPECT_TRUE(condition)                                                                     \
  do                                                                                               \
  {                                                                                                \
    if (!(condition))                                                                              \
    {                                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": expected " #condition "\n";                   \
      ++failures;                                                                                  \
    }                                                                                              \
  } while (false)

#define EXPECT_EQ(actual, expected)                                                                \
  do                                                                                               \
  {                                                                                                \
    if (!((actual) == (expected)))                                                                 \
    {                                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": equality failed: " #actual " vs "             \
                << #expected << "\n";                                                              \
      ++failures;                                                                                  \
    }                                                                                              \
  } while (false)

RawGpsObservation FixedObservation()
{
  RawGpsObservation observation;
  observation.mavlink_time_usec = 1'234'567u;
  observation.fix_type = 6u;
  observation.latitude_deg_e7 = 481'173'000;
  observation.longitude_deg_e7 = 115'166'667;
  observation.altitude_msl_mm = 545'400;
  observation.hdop_centi = 90u;
  observation.vdop_centi = 120u;
  observation.ground_speed_cm_s = 250u;
  observation.course_cdeg = 12'345u;
  observation.satellites_visible = 18u;
  observation.horizontal_accuracy_mm = 14u;
  observation.vertical_accuracy_mm = 25u;
  return observation;
}

MavlinkGnssAdapter NewAdapter() { return MavlinkGnssAdapter({"mavlink:test-fcu", "test-process"}); }

void TestGps1Observation()
{
  auto adapter = NewAdapter();
  adapter.HandleRawGps(Receiver::kGps1, FixedObservation(), 42'000u);
  const auto snapshot = adapter.Snapshot(Receiver::kGps1);

  EXPECT_TRUE(snapshot.state.fix_valid);
  EXPECT_EQ(snapshot.state.fix_type, universal_gnss::GnssFixType::kRtkFixed);
  EXPECT_EQ(snapshot.state.timestamp_ns, 42'000u);
  EXPECT_TRUE(snapshot.state.latitude_deg.has_value());
  EXPECT_TRUE(std::abs(*snapshot.state.latitude_deg - 48.1173) < 1.0e-9);
  EXPECT_TRUE(snapshot.state.altitude_m.has_value());
  EXPECT_TRUE(std::abs(*snapshot.state.altitude_m - 545.4) < 1.0e-9);
  EXPECT_EQ(snapshot.position_observation_sequence, 1u);
  EXPECT_EQ(snapshot.source_id, std::string("mavlink:test-fcu:gps1"));
  EXPECT_TRUE(universal_gnss::HasValueAvailable(snapshot.state, GnssCapability::kRtkMode));
}

void TestGps2ObservationAndAvailability()
{
  auto adapter = NewAdapter();
  auto observation = FixedObservation();
  observation.fix_type = 5u;
  observation.supports_dgps_age = true;
  observation.dgps_age_ms = 2750u;
  adapter.HandleRawGps(Receiver::kGps2, observation, 84'000u);
  const auto snapshot = adapter.Snapshot(Receiver::kGps2);

  EXPECT_EQ(snapshot.state.fix_type, universal_gnss::GnssFixType::kRtkFloat);
  EXPECT_EQ(snapshot.source_id, std::string("mavlink:test-fcu:gps2"));
  EXPECT_TRUE(snapshot.state.correction_age_s.has_value());
  EXPECT_TRUE(std::abs(*snapshot.state.correction_age_s - 2.75f) < 1.0e-6f);
  EXPECT_TRUE(universal_gnss::HasValueAvailable(snapshot.state, GnssCapability::kCorrectionAge));
}

void TestIdenticalObservationsAdvanceSequence()
{
  auto adapter = NewAdapter();
  const auto observation = FixedObservation();
  adapter.HandleRawGps(Receiver::kGps1, observation, 100u);
  adapter.HandleRawGps(Receiver::kGps1, observation, 200u);
  const auto snapshot = adapter.Snapshot(Receiver::kGps1);

  EXPECT_EQ(snapshot.position_observation_sequence, 2u);
  EXPECT_EQ(snapshot.state.timestamp_ns, 200u);
}

void TestCurrentUnavailableValuesClearPriorRawCache()
{
  auto adapter = NewAdapter();
  adapter.HandleRawGps(Receiver::kGps1, FixedObservation(), 100u);

  auto unavailable = FixedObservation();
  unavailable.fix_type = 255u;
  unavailable.hdop_centi = UINT16_MAX;
  unavailable.vdop_centi = UINT16_MAX;
  unavailable.ground_speed_cm_s = UINT16_MAX;
  unavailable.course_cdeg = UINT16_MAX;
  unavailable.satellites_visible = UINT8_MAX;
  unavailable.horizontal_accuracy_mm.reset();
  unavailable.vertical_accuracy_mm.reset();
  adapter.HandleRawGps(Receiver::kGps1, unavailable, 200u);
  const auto snapshot = adapter.Snapshot(Receiver::kGps1);

  EXPECT_TRUE(!snapshot.state.fix_valid);
  EXPECT_EQ(snapshot.state.fix_type, universal_gnss::GnssFixType::kNoFix);
  EXPECT_TRUE(!snapshot.state.latitude_deg.has_value());
  EXPECT_TRUE(!snapshot.state.horizontal_accuracy_m.has_value());
  EXPECT_TRUE(!snapshot.state.hdop.has_value());
  EXPECT_TRUE(!snapshot.state.speed_over_ground_m_s.has_value());
  EXPECT_TRUE(!snapshot.state.satellites_visible.has_value());
  EXPECT_EQ(snapshot.position_observation_sequence, 2u);
}

void TestReconnectStartsNewIncarnationsAndInvalidatesCaches()
{
  auto adapter = NewAdapter();
  EXPECT_TRUE(adapter.OnConnectionChanged(true));
  adapter.HandleRawGps(Receiver::kGps1, FixedObservation(), 100u);
  adapter.HandleRawGps(Receiver::kGps2, FixedObservation(), 101u);
  const auto connected_incarnation = adapter.Snapshot(Receiver::kGps1).source_incarnation;

  EXPECT_TRUE(adapter.OnConnectionChanged(false));
  const auto disconnected_gps1 = adapter.Snapshot(Receiver::kGps1);
  const auto disconnected_gps2 = adapter.Snapshot(Receiver::kGps2);
  EXPECT_TRUE(disconnected_gps1.source_incarnation != connected_incarnation);
  EXPECT_TRUE(!disconnected_gps1.state.timestamp_ns.has_value());
  EXPECT_TRUE(!disconnected_gps2.state.timestamp_ns.has_value());
  EXPECT_EQ(disconnected_gps1.position_observation_sequence, 0u);
  EXPECT_TRUE(!disconnected_gps1.raw_metadata.has_value());

  EXPECT_TRUE(adapter.OnConnectionChanged(true));
  const auto reconnected = adapter.Snapshot(Receiver::kGps1);
  EXPECT_TRUE(reconnected.source_incarnation != disconnected_gps1.source_incarnation);
  EXPECT_TRUE(reconnected.fcu_connected);
  EXPECT_TRUE(!adapter.OnConnectionChanged(true));
  EXPECT_EQ(adapter.Snapshot(Receiver::kGps1).source_incarnation, reconnected.source_incarnation);
}

void TestBootRegressionStartsIncarnationAndInvalidatesRtk()
{
  auto adapter = NewAdapter();
  adapter.HandleSystemTime({1'000'000u, 50'000u});
  adapter.HandleRawGps(Receiver::kGps1, FixedObservation(), 100u);
  RtkObservation rtk;
  rtk.satellites_used = 12u;
  adapter.HandleRtk(Receiver::kGps2, rtk, 110u);
  const auto previous_incarnation = adapter.Snapshot(Receiver::kGps1).source_incarnation;

  EXPECT_TRUE(adapter.HandleSystemTime({2'000'000u, 100u}));
  const auto gps1 = adapter.Snapshot(Receiver::kGps1);
  const auto gps2 = adapter.Snapshot(Receiver::kGps2);
  EXPECT_TRUE(gps1.source_incarnation != previous_incarnation);
  EXPECT_TRUE(!gps1.state.timestamp_ns.has_value());
  EXPECT_TRUE(!gps2.state.timestamp_ns.has_value());
  EXPECT_TRUE(!gps2.rtk_metadata.has_value());
  EXPECT_TRUE(gps1.system_time_metadata.has_value());
  EXPECT_EQ(gps1.system_time_metadata->boot_time_ms, 100u);
}

void TestBootTimerWrapAndForwardProgressDoNotReset()
{
  auto adapter = NewAdapter();
  adapter.HandleRawGps(Receiver::kGps1, FixedObservation(), 100u);
  EXPECT_TRUE(!adapter.HandleSystemTime({0u, UINT32_MAX - 5u}));
  const auto incarnation = adapter.Snapshot(Receiver::kGps1).source_incarnation;
  EXPECT_TRUE(!adapter.HandleSystemTime({0u, 7u}));
  EXPECT_TRUE(!adapter.HandleSystemTime({0u, 7u}));
  EXPECT_TRUE(!adapter.HandleSystemTime({0u, 10'007u}));
  const auto later = adapter.Snapshot(Receiver::kGps1);
  EXPECT_EQ(later.source_incarnation, incarnation);
  EXPECT_EQ(later.position_observation_sequence, 1u);
  EXPECT_EQ(later.state.timestamp_ns, 100u);
}

void TestReceiverIndependenceAndRtkIsNotPosition()
{
  auto adapter = NewAdapter();
  adapter.HandleRawGps(Receiver::kGps1, FixedObservation(), 100u);
  const auto gps1_before = adapter.Snapshot(Receiver::kGps1);

  auto gps2_observation = FixedObservation();
  gps2_observation.latitude_deg_e7 = -335'000'000;
  adapter.HandleRawGps(Receiver::kGps2, gps2_observation, 200u);
  RtkObservation rtk;
  rtk.rtk_receiver_id = 2u;
  rtk.satellites_used = 9u;
  adapter.HandleRtk(Receiver::kGps2, rtk, 300u);

  const auto gps1_after = adapter.Snapshot(Receiver::kGps1);
  const auto gps2 = adapter.Snapshot(Receiver::kGps2);
  EXPECT_EQ(gps1_after.state.timestamp_ns, gps1_before.state.timestamp_ns);
  EXPECT_EQ(gps1_after.position_observation_sequence, 1u);
  EXPECT_EQ(gps2.position_observation_sequence, 1u);
  EXPECT_EQ(gps2.state.timestamp_ns, 300u);
  EXPECT_TRUE(gps2.state.latitude_deg.has_value());
  EXPECT_TRUE(*gps2.state.latitude_deg < 0.0);
  EXPECT_TRUE(gps2.rtk_metadata.has_value());
}

void TestLateRebootEvidenceInvalidatesButCannotPreventEarlierPublication()
{
  auto adapter = NewAdapter();
  adapter.HandleSystemTime({0u, 60'000u});
  const auto old_incarnation = adapter.Snapshot(Receiver::kGps1).source_incarnation;
  adapter.HandleRawGps(Receiver::kGps1, FixedObservation(), 1'000u);
  EXPECT_EQ(adapter.Snapshot(Receiver::kGps1).source_incarnation, old_incarnation);
  EXPECT_EQ(adapter.Snapshot(Receiver::kGps1).position_observation_sequence, 1u);

  EXPECT_TRUE(adapter.HandleSystemTime({0u, 50u}));
  const auto invalidated = adapter.Snapshot(Receiver::kGps1);
  EXPECT_TRUE(invalidated.source_incarnation != old_incarnation);
  EXPECT_EQ(invalidated.position_observation_sequence, 0u);
  EXPECT_TRUE(!invalidated.state.timestamp_ns.has_value());
}

} // namespace

int main()
{
  TestGps1Observation();
  TestGps2ObservationAndAvailability();
  TestIdenticalObservationsAdvanceSequence();
  TestCurrentUnavailableValuesClearPriorRawCache();
  TestReconnectStartsNewIncarnationsAndInvalidatesCaches();
  TestBootRegressionStartsIncarnationAndInvalidatesRtk();
  TestBootTimerWrapAndForwardProgressDoNotReset();
  TestReceiverIndependenceAndRtkIsNotPosition();
  TestLateRebootEvidenceInvalidatesButCannotPreventEarlierPublication();

  if (failures != 0)
  {
    std::cerr << failures << " MAVLink GNSS adapter checks failed\n";
    return 1;
  }
  return 0;
}
