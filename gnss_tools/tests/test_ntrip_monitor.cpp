#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss_tools/ntrip_monitor.hpp"

namespace
{

using universal_gnss::GnssDiagnosticSeverity;
using universal_gnss_protocols::RtcmConstellation;
using universal_gnss_protocols::RtcmCorrectionMonitor;
using universal_gnss_protocols::RtcmFrame;
using universal_gnss_protocols::RtcmMessageInfo;
using universal_gnss_tools::BuildNtripMonitorConfig;
using universal_gnss_tools::BuildNtripMonitorRuntimeState;
using universal_gnss_tools::BuildNtripMonitorSnapshot;
using universal_gnss_tools::FormatNtripMonitorStatusLine;
using universal_gnss_tools::FormatNtripMonitorSummaryJson;
using universal_gnss_tools::FormatNtripMonitorSummaryText;
using universal_gnss_tools::NtripMonitorOptions;
using universal_gnss_tools::NtripMonitorStopReason;
using universal_gnss_tools::NtripMonitorValidationError;
using universal_gnss_tools::ValidateNtripMonitorOptions;

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

NtripMonitorOptions MakeOptions()
{
  NtripMonitorOptions options;
  options.host = "caster.example.org";
  options.port = 2101u;
  options.mountpoint = "NEAR";
  options.user_agent = "universal-gnss-test";
  return options;
}

void ObserveMessage(RtcmCorrectionMonitor& monitor,
                    const std::uint16_t message_type,
                    const std::int64_t timestamp_ns,
                    const bool is_station_arp = false,
                    const bool is_glonass_bias = false,
                    const RtcmConstellation constellation = RtcmConstellation::kUnknown)
{
  RtcmMessageInfo info;
  info.message_type = message_type;
  info.is_station_arp = is_station_arp;
  info.is_glonass_bias = is_glonass_bias;
  info.msm_constellation = constellation;
  info.is_msm = constellation != RtcmConstellation::kUnknown;
  monitor.ObserveMessage(info, timestamp_ns);
}

void AppendBit(std::vector<std::uint8_t>& payload, std::size_t& bit_offset, const bool bit)
{
  if ((bit_offset % 8u) == 0u)
  {
    payload.push_back(0u);
  }

  if (bit)
  {
    payload.back() |= static_cast<std::uint8_t>(1u << (7u - (bit_offset % 8u)));
  }
  ++bit_offset;
}

void AppendUnsignedBits(std::vector<std::uint8_t>& payload,
                        std::size_t& bit_offset,
                        const std::uint64_t value,
                        const std::size_t bit_count)
{
  for (std::size_t i = 0u; i < bit_count; ++i)
  {
    const std::size_t shift = bit_count - 1u - i;
    AppendBit(payload, bit_offset, ((value >> shift) & 0x01u) != 0u);
  }
}

void AppendSignedBits(std::vector<std::uint8_t>& payload,
                      std::size_t& bit_offset,
                      const std::int64_t value,
                      const std::size_t bit_count)
{
  const std::uint64_t mask = (1ULL << bit_count) - 1ULL;
  AppendUnsignedBits(payload, bit_offset, static_cast<std::uint64_t>(value) & mask, bit_count);
}

RtcmFrame BuildRtcm1230Frame(const std::int64_t timestamp_ns)
{
  RtcmFrame frame;
  frame.protocol = universal_gnss_protocols::ProtocolType::kRtcm3;
  frame.timestamp_ns = timestamp_ns;
  frame.checksum_status = universal_gnss_protocols::ChecksumStatus::kValid;
  frame.raw_bytes = {0xD3u};
  frame.message_type = 1230u;

  std::size_t bit_offset = 0u;
  AppendUnsignedBits(frame.payload, bit_offset, 1230u, 12u);
  AppendUnsignedBits(frame.payload, bit_offset, 42u, 12u);
  AppendUnsignedBits(frame.payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(frame.payload, bit_offset, 0u, 3u);
  AppendUnsignedBits(frame.payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(frame.payload, bit_offset, 0u, 1u);
  AppendUnsignedBits(frame.payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(frame.payload, bit_offset, 1u, 1u);
  AppendSignedBits(frame.payload, bit_offset, 10, 16u);
  AppendSignedBits(frame.payload, bit_offset, -5, 16u);
  AppendSignedBits(frame.payload, bit_offset, 7, 16u);
  return frame;
}

void TestOptionValidation(TestContext& ctx)
{
  {
    const auto valid = ValidateNtripMonitorOptions(MakeOptions());
    ctx.Expect(valid.ok(), "complete monitor options should validate");
  }

  {
    auto options = MakeOptions();
    options.host.clear();
    const auto result = ValidateNtripMonitorOptions(options);
    ctx.Expect(result.error == NtripMonitorValidationError::kMissingHost,
               "validation should require a host");
  }

  {
    auto options = MakeOptions();
    options.latitude_deg = 48.0;
    const auto result = ValidateNtripMonitorOptions(options);
    ctx.Expect(result.error == NtripMonitorValidationError::kMissingLongitude,
               "validation should require longitude when latitude is provided");
  }

  {
    auto options = MakeOptions();
    options.gga_interval_s = 5u;
    const auto result = ValidateNtripMonitorOptions(options);
    ctx.Expect(result.error == NtripMonitorValidationError::kGgaIntervalRequiresPosition,
               "validation should reject periodic GGA without a position source");
  }
}

void TestConfigAndRuntimeStateBuilders(TestContext& ctx)
{
  auto options = MakeOptions();
  options.username = "user";
  options.password = "pass";
  options.latitude_deg = 48.0;
  options.longitude_deg = 2.0;
  options.altitude_m = 120.5;
  options.gga_interval_s = 5u;

  const auto config = BuildNtripMonitorConfig(options);
  ctx.Expect(config.host == "caster.example.org" &&
                 config.port == 2101u &&
                 config.mountpoint == "NEAR" &&
                 config.username == "user" &&
                 config.password == "pass" &&
                 config.user_agent == "universal-gnss-test" &&
                 config.send_gga &&
                 config.gga_interval_s == 5u,
             "config builder should map monitor options into the portable NTRIP config");

  const auto runtime_state = BuildNtripMonitorRuntimeState(options);
  ctx.Expect(runtime_state.has_value() &&
                 runtime_state->fix_valid &&
                 runtime_state->latitude_deg == std::optional<double>(48.0) &&
                 runtime_state->longitude_deg == std::optional<double>(2.0) &&
                 runtime_state->altitude_m == std::optional<double>(120.5),
             "runtime-state builder should produce a usable portable GGA source");
}

void TestSummaryFormatting(TestContext& ctx)
{
  auto options = MakeOptions();
  options.latitude_deg = 48.0;
  options.longitude_deg = 2.0;
  options.gga_interval_s = 5u;

  RtcmCorrectionMonitor correction_monitor;
  ObserveMessage(correction_monitor, 1005u, 1000000000LL, true);
  ObserveMessage(correction_monitor, 1077u, 1500000000LL, false, false, RtcmConstellation::kGps);
  correction_monitor.ObserveFrame(BuildRtcm1230Frame(2000000000LL));
  correction_monitor.ObserveInvalidFrame(2200000000LL);

  universal_gnss_ntrip::NtripConnectionMetrics metrics;
  metrics.bytes_received = 4096u;
  metrics.bytes_sent = 128u;
  metrics.rtcm_frames_seen = 4u;
  metrics.rtcm_frames_received = 3u;
  metrics.invalid_rtcm_frames = 1u;
  metrics.gga_sent_count = 2u;
  metrics.gga_send_errors = 1u;
  metrics.last_rtcm_message_type = 1230u;
  metrics.last_gga_sent_timestamp_ns = 2100000000LL;
  metrics.request_sent = true;
  metrics.response_received = true;
  metrics.reconnect_count = 1u;
  metrics.last_error = universal_gnss_ntrip::NtripClientError::kTimeout;

  universal_gnss::GnssHealthSummary health;
  health.correction_available = true;
  health.AddEvent({GnssDiagnosticSeverity::kWarning,
                   universal_gnss::GnssDiagnosticCategory::kCorrection,
                   "rtcm.stale",
                   "stream is getting stale",
                   2500000000LL,
                   "ntrip"});

  const auto snapshot = BuildNtripMonitorSnapshot(options,
                                                  "streaming",
                                                  metrics,
                                                  correction_monitor,
                                                  health,
                                                  NtripMonitorStopReason::kMaxSeconds,
                                                  30000000000LL,
                                                  "ICY 200 OK\r\nNtrip-Version: Ntrip/2.0\r\n\r\n",
                                                  2500000000LL);

  const std::string status = FormatNtripMonitorStatusLine(snapshot);
  const std::string text = FormatNtripMonitorSummaryText(snapshot);
  const std::string json = FormatNtripMonitorSummaryJson(snapshot);

  ctx.Expect(status.find("state=streaming") != std::string::npos &&
                 status.find("health=warning") != std::string::npos &&
                 status.find("last_type=1230") != std::string::npos,
             "status formatting should surface state, severity, and the last RTCM type");
  ctx.Expect(text.find("endpoint=caster.example.org:2101/NEAR state=streaming stop_reason=max_seconds") !=
                 std::string::npos &&
                 text.find("message_types 1005=1 1077=1 1230=1") != std::string::npos &&
                 text.find("msm_constellations gps=1") != std::string::npos &&
                 text.find("response_status ICY 200 OK") != std::string::npos &&
                 text.find("glonass_bias_1230_seen=true") != std::string::npos &&
                 text.find("glonass_code_phase_bias seen=true decoded=true valid=true") !=
                     std::string::npos &&
                 text.find("signal_mask=0xD") != std::string::npos &&
                 text.find("age_ns=500000000") != std::string::npos,
             "text formatting should summarize message counts, constellation counts, and status");
  ctx.Expect(json.find("\"stop_reason\":\"max_seconds\"") != std::string::npos &&
                 json.find("\"message_type_counts\":{\"1005\":1,\"1077\":1,\"1230\":1}") !=
                     std::string::npos &&
                 json.find("\"msm_constellation_counts\":{\"gps\":1}") !=
                     std::string::npos &&
                 json.find("\"semantic_observations\":[") != std::string::npos &&
                 json.find("\"name\":\"glonass_code_phase_bias\"") != std::string::npos &&
                 json.find("\"signal_mask\":\"0xD\"") != std::string::npos &&
                 json.find("\"response_status_line\":\"ICY 200 OK\"") !=
                     std::string::npos,
             "JSON formatting should emit stable structured monitor summary fields");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestOptionValidation(ctx);
  TestConfigAndRuntimeStateBuilders(ctx);
  TestSummaryFormatting(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools NTRIP monitor tests passed\n";
  return EXIT_SUCCESS;
}
