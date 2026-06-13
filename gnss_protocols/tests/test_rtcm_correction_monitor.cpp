#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include "universal_gnss_protocols/rtcm_records.hpp"

namespace
{

using universal_gnss::GnssDiagnosticSeverity;
using universal_gnss::GnssHealthSummary;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::RtcmConstellation;
using universal_gnss_protocols::RtcmCorrectionHealthOptions;
using universal_gnss_protocols::RtcmCorrectionMonitor;
using universal_gnss_protocols::RtcmFrame;
using universal_gnss_protocols::RtcmMessageInfo;

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

  void ExpectNear(double lhs, double rhs, double epsilon, const std::string& message)
  {
    if (std::fabs(lhs - rhs) > epsilon)
    {
      ++failures;
      std::cerr << "FAILED: " << message << " lhs=" << lhs << " rhs=" << rhs << '\n';
    }
  }
};

std::vector<std::uint8_t> MakeRtcmPayload(const std::uint16_t message_type)
{
  return {
      static_cast<std::uint8_t>((message_type >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((message_type & 0x0Fu) << 4u),
  };
}

RtcmFrame MakeValidRtcmFrame(const std::uint16_t message_type,
                             const std::optional<std::int64_t> timestamp_ns = std::nullopt)
{
  RtcmFrame frame;
  frame.timestamp_ns = timestamp_ns;
  frame.payload = MakeRtcmPayload(message_type);
  frame.checksum_status = ChecksumStatus::kValid;
  return frame;
}

RtcmMessageInfo MakeMessageInfo(const std::uint16_t message_type)
{
  RtcmMessageInfo info;
  info.message_type = message_type;
  info.is_station_arp = message_type == 1005u || message_type == 1006u;
  info.is_glonass_bias = message_type == 1230u;
  switch (message_type)
  {
    case 1074u:
    case 1077u:
      info.is_msm = true;
      info.msm_constellation = RtcmConstellation::kGps;
      break;
    case 1087u:
      info.is_msm = true;
      info.msm_constellation = RtcmConstellation::kGlonass;
      break;
    case 1097u:
      info.is_msm = true;
      info.msm_constellation = RtcmConstellation::kGalileo;
      break;
    case 1127u:
      info.is_msm = true;
      info.msm_constellation = RtcmConstellation::kBeiDou;
      break;
    default:
      break;
  }
  return info;
}

void TestMessageCountsAndLastSeen(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1005u), 100);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 200);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 350);

  ctx.Expect(monitor.total_frames() == 3u, "valid observations should increment total frame count");
  ctx.Expect(monitor.valid_frames() == 3u, "valid observations should increment valid frame count");
  ctx.Expect(monitor.invalid_frames() == 0u, "valid observations should not increment invalid count");
  ctx.Expect(monitor.last_frame_timestamp_ns() == std::optional<std::int64_t>(350),
             "monitor should retain the latest frame timestamp");
  ctx.Expect(monitor.MessageCount(1005u) == 1u, "message type 1005 should be counted once");
  ctx.Expect(monitor.MessageCount(1077u) == 2u, "message type 1077 should be counted twice");
  ctx.Expect(monitor.LastSeenMessageTimestampNs(1077u) == std::optional<std::int64_t>(350),
             "last-seen timestamp should track the latest 1077 message");
  ctx.Expect(monitor.AgeSinceMessageTypeNs(1077u, 500) == std::optional<std::int64_t>(150),
             "age helper should subtract the latest message timestamp from now");
  ctx.Expect(monitor.HasRequiredMessageTypes({1005u, 1077u}),
             "required-message helper should succeed when all message types were observed");
}

void TestRateHelpers(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1077u), 1000000000LL);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 2000000000LL);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 6000000000LL);
  monitor.ObserveInvalidFrame(6500000000LL);

  const auto message_rate_hz = monitor.MessageRateHz(1077u, 6000000000LL, 4000000000LL);
  ctx.Expect(message_rate_hz.has_value(), "timestamped message observations should produce a rate");
  if (message_rate_hz.has_value())
  {
    ctx.ExpectNear(*message_rate_hz, 0.5, 1e-9, "1077 rate should count only timestamps in window");
  }

  const auto total_frame_rate_hz = monitor.TotalFrameRateHz(6500000000LL, 1000000000LL);
  ctx.Expect(total_frame_rate_hz.has_value(), "timestamped frames should produce a total rate");
  if (total_frame_rate_hz.has_value())
  {
    ctx.ExpectNear(*total_frame_rate_hz, 2.0, 1e-9, "frame-rate helper should include valid and invalid frames");
  }
}

void TestMsmConstellationTracking(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1074u), 100000000LL);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 200000000LL);
  monitor.ObserveMessage(MakeMessageInfo(1087u), 250000000LL);

  ctx.Expect(monitor.HasSeenAnyMsmMessage(), "MSM observation should be tracked");
  ctx.Expect(monitor.MsmConstellationCount(RtcmConstellation::kGps) == 2u,
             "GPS MSM count should aggregate multiple GPS MSM messages");
  ctx.Expect(monitor.MsmConstellationCount(RtcmConstellation::kGlonass) == 1u,
             "GLONASS MSM count should reflect observed GLONASS MSM messages");
  ctx.Expect(monitor.LastSeenMsmConstellationTimestampNs(RtcmConstellation::kGps) ==
                 std::optional<std::int64_t>(200000000LL),
             "last-seen timestamp should be retained per MSM constellation");
  ctx.Expect(monitor.AgeSinceMsmConstellationNs(RtcmConstellation::kGlonass, 400000000LL) ==
                 std::optional<std::int64_t>(150000000LL),
             "MSM age helper should use the latest constellation timestamp");

  const auto gps_rate_hz =
      monitor.MsmConstellationRateHz(RtcmConstellation::kGps, 200000000LL, 200000000LL);
  ctx.Expect(gps_rate_hz.has_value(), "timestamped MSM constellations should produce a rate");
  if (gps_rate_hz.has_value())
  {
    ctx.ExpectNear(*gps_rate_hz, 10.0, 1e-9, "MSM constellation rate should be windowed");
  }
}

void TestBasePositionAndGlonassBiasTracking(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1005u), 100);
  monitor.ObserveMessage(MakeMessageInfo(1006u), 200);
  monitor.ObserveMessage(MakeMessageInfo(1230u), 300);

  ctx.Expect(monitor.HasSeenBasePositionMessage(),
             "station ARP observations should mark base-position availability");
  ctx.Expect(monitor.HasSeenBasePosition1005(), "1005 should be tracked individually");
  ctx.Expect(monitor.HasSeenBasePosition1006(), "1006 should be tracked individually");
  ctx.Expect(monitor.HasSeenGlonassBias1230(), "1230 should be tracked as seen");
  RtcmCorrectionHealthOptions options;
  options.required_message_types = {1230u};
  options.require_base_position = true;
  options.require_glonass_bias = true;
  ctx.Expect(monitor.HasRequiredCorrectionMessages(options),
             "required-corrections helper should reflect base and 1230 observations");
}

void TestInvalidFrameHandling(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveFrame(MakeValidRtcmFrame(1077u, 100));

  RtcmFrame invalid_checksum = MakeValidRtcmFrame(1077u, 200);
  invalid_checksum.checksum_status = ChecksumStatus::kInvalid;
  monitor.ObserveFrame(invalid_checksum);

  RtcmFrame truncated_payload = MakeValidRtcmFrame(1087u, 300);
  truncated_payload.payload = {0x43u};
  monitor.ObserveFrame(truncated_payload);

  ctx.Expect(monitor.total_frames() == 3u, "all frame observations should increment total count");
  ctx.Expect(monitor.valid_frames() == 1u, "only checksum-valid, parseable frames should be valid");
  ctx.Expect(monitor.invalid_frames() == 2u, "invalid frames should be counted");
  ctx.Expect(monitor.MessageCount(1077u) == 1u, "invalid frames should not populate message counts");
  ctx.Expect(monitor.last_frame_timestamp_ns() == std::optional<std::int64_t>(300),
             "latest timestamp should include invalid frames");
}

void TestHealthStates(TestContext& ctx)
{
  RtcmCorrectionMonitor healthy_monitor;
  healthy_monitor.ObserveMessage(MakeMessageInfo(1077u), 1000);

  RtcmCorrectionHealthOptions healthy_options;
  healthy_options.now_timestamp_ns = 1500;
  healthy_options.stale_after_ns = 1000;
  healthy_options.require_any_msm = true;
  const GnssHealthSummary healthy = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      healthy_monitor,
      healthy_options);
  ctx.Expect(healthy.overall_severity == GnssDiagnosticSeverity::kOk,
             "recent RTCM activity should yield ok health");
  ctx.Expect(healthy.correction_available, "recent required RTCM activity should be available");
  ctx.Expect(!healthy.stale_data, "recent RTCM activity should not be marked stale");

  RtcmCorrectionHealthOptions stale_options;
  stale_options.now_timestamp_ns = 3000;
  stale_options.stale_after_ns = 1000;
  stale_options.require_any_msm = true;
  const GnssHealthSummary stale = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      healthy_monitor,
      stale_options);
  ctx.Expect(stale.overall_severity == GnssDiagnosticSeverity::kWarning,
             "stale RTCM activity should yield a warning");
  ctx.Expect(stale.stale_data, "stale RTCM activity should set stale_data");
  ctx.Expect(!stale.correction_available, "stale RTCM activity should not report current availability");

  RtcmCorrectionMonitor unknown_monitor;
  unknown_monitor.ObserveMessage(MakeMessageInfo(1077u));
  RtcmCorrectionHealthOptions unknown_options;
  unknown_options.now_timestamp_ns = 5000;
  unknown_options.stale_after_ns = 1000;
  unknown_options.require_any_msm = true;
  const GnssHealthSummary unknown = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      unknown_monitor,
      unknown_options);
  ctx.Expect(unknown.overall_severity == GnssDiagnosticSeverity::kUnknown,
             "timestamp-less RTCM activity should yield unknown freshness");

  RtcmCorrectionMonitor missing_required_monitor;
  missing_required_monitor.ObserveMessage(MakeMessageInfo(1005u), 1000);
  RtcmCorrectionHealthOptions error_options;
  error_options.now_timestamp_ns = 1200;
  error_options.stale_after_ns = 1000;
  error_options.required_message_types = {1077u};
  error_options.require_base_position = true;
  const GnssHealthSummary error = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      missing_required_monitor,
      error_options);
  ctx.Expect(error.overall_severity == GnssDiagnosticSeverity::kError,
             "missing required correction content should yield an error");
  ctx.Expect(error.HasErrors(), "missing required correction content should emit an error event");
}

void TestPortableRtkRequirementsAccept1006(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1006u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 1100);
  monitor.ObserveMessage(MakeMessageInfo(1087u), 1200);
  monitor.ObserveMessage(MakeMessageInfo(1097u), 1300);
  monitor.ObserveMessage(MakeMessageInfo(1127u), 1400);
  monitor.ObserveMessage(MakeMessageInfo(1230u), 1500);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 2000;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 10000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  const GnssHealthSummary health = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      monitor,
      options);
  ctx.Expect(monitor.HasRequiredCorrectionMessages(options),
             "portable RTK requirements should accept 1006 as the base-position message");
  ctx.Expect(health.correction_available,
             "complete portable RTCM content should report correction availability");
  ctx.Expect(health.overall_severity == GnssDiagnosticSeverity::kOk,
             "complete portable RTCM content should clear the missing-message diagnostic");
}

void TestPortableRtkRequirementsUseRecentObservationWindow(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1005u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1077u), 9000);
  monitor.ObserveMessage(MakeMessageInfo(1087u), 9100);
  monitor.ObserveMessage(MakeMessageInfo(1097u), 9200);
  monitor.ObserveMessage(MakeMessageInfo(1127u), 9300);
  monitor.ObserveMessage(MakeMessageInfo(1230u), 9400);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 12000;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 2000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  const GnssHealthSummary health = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      monitor,
      options);
  ctx.Expect(!monitor.HasRequiredCorrectionMessages(options),
             "portable RTK requirements should expire base-position messages outside the recent window");
  ctx.Expect(health.overall_severity == GnssDiagnosticSeverity::kError,
             "missing recent required RTCM content should remain an error after startup grace");
}

void TestPortableRtkRequirementsRespectStartupGrace(TestContext& ctx)
{
  RtcmCorrectionMonitor monitor;
  monitor.ObserveMessage(MakeMessageInfo(1077u), 1000);
  monitor.ObserveMessage(MakeMessageInfo(1087u), 1100);

  RtcmCorrectionHealthOptions options;
  options.now_timestamp_ns = 2500;
  options.stale_after_ns = 5000;
  options.required_observation_window_ns = 10000;
  options.startup_grace_ns = 5000;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);

  const GnssHealthSummary health = universal_gnss_protocols::BuildRtcmCorrectionHealth(
      monitor,
      options);
  ctx.Expect(health.overall_severity == GnssDiagnosticSeverity::kInfo,
             "startup grace should defer the missing required RTCM error while the stream is still collecting");
  ctx.Expect(!health.HasErrors(),
             "startup grace should avoid emitting a hard required-message error");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestMessageCountsAndLastSeen(ctx);
  TestRateHelpers(ctx);
  TestMsmConstellationTracking(ctx);
  TestBasePositionAndGlonassBiasTracking(ctx);
  TestInvalidFrameHandling(ctx);
  TestHealthStates(ctx);
  TestPortableRtkRequirementsAccept1006(ctx);
  TestPortableRtkRequirementsUseRecentObservationWindow(ctx);
  TestPortableRtkRequirementsRespectStartupGrace(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols RTCM correction monitor tests passed\n";
  return EXIT_SUCCESS;
}
