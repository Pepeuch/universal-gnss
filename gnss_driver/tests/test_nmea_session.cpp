#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/nmea_session.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"

namespace
{

using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::HasCapability;
using universal_gnss::HasValueAvailable;
using universal_gnss_driver::NmeaSession;

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

bool NearlyEqual(const double lhs, const double rhs, const double tolerance = 1e-6)
{
  const double delta = lhs - rhs;
  return delta <= tolerance && delta >= -tolerance;
}

std::vector<std::uint8_t> BuildNmeaSentence(const std::string& payload,
                                            const bool valid_checksum = true)
{
  std::vector<std::uint8_t> bytes;
  bytes.push_back(static_cast<std::uint8_t>('$'));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.push_back(static_cast<std::uint8_t>('*'));

  std::uint8_t checksum = universal_gnss_protocols::ComputeNmeaChecksum(payload);
  if (!valid_checksum)
  {
    checksum ^= 0x01u;
  }

  constexpr char kHexDigits[] = "0123456789ABCDEF";
  bytes.push_back(static_cast<std::uint8_t>(kHexDigits[(checksum >> 4u) & 0x0Fu]));
  bytes.push_back(static_cast<std::uint8_t>(kHexDigits[checksum & 0x0Fu]));
  bytes.push_back(static_cast<std::uint8_t>('\r'));
  bytes.push_back(static_cast<std::uint8_t>('\n'));
  return bytes;
}

void TestPositionAndFixUpdates(TestContext& ctx)
{
  NmeaSession session;
  session.FeedBytes(
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),
      1000);

  const auto& state = session.current_state();
  const auto& metrics = session.metrics();
  ctx.Expect(state.timestamp_ns == std::optional<std::int64_t>(1000) &&
                 state.fix_valid &&
                 state.fix_type == GnssFixType::kFix &&
                 HasCapability(state, universal_gnss::GnssCapability::kRtkMode) &&
                 HasValueAvailable(state, universal_gnss::GnssCapability::kRtkMode) &&
                 state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kNone) &&
                 state.latitude_deg.has_value() &&
                 state.longitude_deg.has_value() &&
                 state.altitude_m == std::optional<double>(545.4),
             "GGA should update the generic NMEA session with fix, position, and a known non-RTK mode");
  ctx.Expect(metrics.sentences_seen == 1u && metrics.records_parsed == 1u &&
                 metrics.runtime_updates == 1u,
             "GGA should count as one parsed runtime-producing sentence");
}

void TestStandardGgaFixQualityDrivesPortableRtkMode(TestContext& ctx)
{
  NmeaSession session;
  session.FeedBytes(
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,4,08,0.9,545.4,M,46.9,M,,"),
      1100);

  ctx.Expect(session.current_state().fix_valid &&
                 session.current_state().fix_type == GnssFixType::kFix &&
                 session.current_state().rtk_mode ==
                     std::optional<GnssRtkMode>(GnssRtkMode::kFixed),
             "GGA fix quality 4 should map to RTK fixed");

  session.FeedBytes(
      BuildNmeaSentence("GPGGA,123520,4807.038,N,01131.000,E,5,08,0.9,545.4,M,46.9,M,,"),
      1101);
  ctx.Expect(session.current_state().fix_valid &&
                 session.current_state().fix_type == GnssFixType::kFix &&
                 session.current_state().rtk_mode ==
                     std::optional<GnssRtkMode>(GnssRtkMode::kFloat),
             "GGA fix quality 5 should map to RTK float");

  session.FeedBytes(
      BuildNmeaSentence("GPGGA,123521,4807.038,N,01131.000,E,2,08,0.9,545.4,M,46.9,M,,"),
      1102);
  ctx.Expect(session.current_state().fix_valid &&
                 session.current_state().fix_type == GnssFixType::kFix &&
                 session.current_state().rtk_mode ==
                     std::optional<GnssRtkMode>(GnssRtkMode::kNone),
             "GGA fix quality 2 should clear RTK float/fixed back to a known non-RTK mode");

  session.FeedBytes(BuildNmeaSentence("GPGGA,123522,,,,,0,00,,,,,,"), 1103);
  ctx.Expect(!session.current_state().fix_valid &&
                 session.current_state().fix_type == GnssFixType::kNoFix &&
                 session.current_state().rtk_mode ==
                     std::optional<GnssRtkMode>(GnssRtkMode::kNone),
             "invalid GGA should not leave stale RTK float/fixed state behind");
}

void TestDopSatelliteCn0AndAccuracyUpdates(TestContext& ctx)
{
  NmeaSession session;
  session.FeedBytes(
      BuildNmeaSentence("GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5"), 2000);
  session.FeedBytes(
      BuildNmeaSentence("GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"),
      2001);
  session.FeedBytes(
      BuildNmeaSentence("GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,0.6,1.1"), 2002);

  const auto& state = session.current_state();
  ctx.Expect(state.hdop == std::optional<float>(1.0f) &&
                 state.vdop == std::optional<float>(1.5f) &&
                 state.satellites_used == std::optional<std::uint16_t>(8u) &&
                 state.satellites_visible == std::optional<std::uint16_t>(8u) &&
                 state.mean_cn0_db_hz.has_value() &&
                 state.max_cn0_db_hz == std::optional<float>(43.0f) &&
                 NearlyEqual(*state.mean_cn0_db_hz, static_cast<float>((41.0 + 43.0 + 42.0 + 39.0) / 4.0)) &&
                 state.horizontal_accuracy_m == std::optional<float>(0.6f) &&
                 state.vertical_accuracy_m == std::optional<float>(1.1f),
             "GSA, GSV, and GST should enrich DOP, satellites, CN0, and accuracy");
}

void TestVtgAndZdaRemainSemanticOnly(TestContext& ctx)
{
  NmeaSession session;
  session.FeedBytes(
      BuildNmeaSentence("GPVTG,054.7,T,034.4,M,005.5,N,010.2,K,A"), 3000);
  session.FeedBytes(
      BuildNmeaSentence("GPZDA,201530.00,04,07,2002,00,00"), 3001);

  const auto& state = session.current_state();
  const auto& metrics = session.metrics();
  ctx.Expect(!state.fix_valid &&
                 state.fix_type == GnssFixType::kUnknown &&
                 !state.latitude_deg.has_value() &&
                 !HasCapability(state, universal_gnss::GnssCapability::kHeading),
             "VTG and ZDA should not invent portable runtime fields");
  ctx.Expect(metrics.records_parsed == 2u &&
                 metrics.semantic_only_records == 2u &&
                 metrics.runtime_updates == 0u,
             "VTG and ZDA should parse as semantic-only NMEA records");
}

void TestMalformedAndResetBehavior(TestContext& ctx)
{
  NmeaSession session;
  session.FeedBytes(
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,",
                        false),
      4000);
  session.FeedString("$GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5*");
  session.Finalize();

  ctx.Expect(session.metrics().malformed_sentences >= 2u,
             "invalid checksum and truncated tail should count as malformed sentences");

  session.Reset();
  ctx.Expect(session.metrics().bytes_seen == 0u &&
                 session.metrics().records_parsed == 0u &&
                 session.current_state().fix_type == GnssFixType::kUnknown &&
                 !session.current_state().fix_valid,
             "reset should clear NMEA session metrics and runtime state");
}

void TestNonFiniteGgaValuesAreRejectedBeforeRuntimeMerge(TestContext& ctx)
{
  constexpr std::string_view kNonFiniteValues[] = {"nan", "+nan", "-nan", "inf", "+inf", "-inf"};

  for (const std::string_view value : kNonFiniteValues)
  {
    NmeaSession session;
    session.FeedBytes(
        BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),
        5000);
    session.FeedBytes(
        BuildNmeaSentence("GPGGA,123520,4807.038,N,01131.000,E,1,08,0.9," +
                          std::string(value) + ",M,46.9,M,,"),
        5001);

    const auto& state = session.current_state();
    const auto& metrics = session.metrics();
    ctx.Expect(metrics.records_parsed == 1u && metrics.records_rejected == 1u &&
                   metrics.runtime_updates == 1u &&
                   state.altitude_m == std::optional<double>(545.4) &&
                   state.altitude_m.has_value() && std::isfinite(*state.altitude_m),
               "non-finite NMEA GGA altitude must be rejected before it can update runtime state: " +
                   std::string(value));
  }
}

}  // namespace

int main()
{
  TestContext ctx;

  TestPositionAndFixUpdates(ctx);
  TestStandardGgaFixQualityDrivesPortableRtkMode(ctx);
  TestDopSatelliteCn0AndAccuracyUpdates(ctx);
  TestVtgAndZdaRemainSemanticOnly(ctx);
  TestMalformedAndResetBehavior(ctx);
  TestNonFiniteGgaValuesAreRejectedBeforeRuntimeMerge(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver NMEA session tests passed\n";
  return EXIT_SUCCESS;
}
