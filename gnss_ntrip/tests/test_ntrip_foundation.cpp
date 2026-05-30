#include <cstdlib>
#include <iostream>
#include <string>

#include "universal_gnss_ntrip/gga_injection_policy.hpp"
#include "universal_gnss_ntrip/ntrip_config.hpp"
#include "universal_gnss_ntrip/ntrip_metrics.hpp"
#include "universal_gnss_ntrip/ntrip_request.hpp"

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

void TestBasicAuthorization(TestContext& ctx)
{
  ctx.Expect(universal_gnss_ntrip::BuildBasicAuthorizationValue("user", "pass") ==
                 "Basic dXNlcjpwYXNz",
             "basic authorization value should be base64(username:password)");
  ctx.Expect(universal_gnss_ntrip::BuildAuthorizationHeader("user", "pass") ==
                 "Authorization: Basic dXNlcjpwYXNz\r\n",
             "authorization header should include the Basic auth value");
  ctx.Expect(universal_gnss_ntrip::BuildBasicAuthorizationValue("", "").empty() &&
                 universal_gnss_ntrip::BuildAuthorizationHeader("", "").empty(),
             "empty credentials should suppress authorization output");
  ctx.Expect(universal_gnss_ntrip::BuildBasicAuthorizationValue("user", "") ==
                 "Basic dXNlcjo=",
             "single-sided credentials should still encode the required colon separator");
}

void TestMountpointNormalization(TestContext& ctx)
{
  ctx.Expect(universal_gnss_ntrip::NormalizeMountpointPath("MOUNT") == "/MOUNT",
             "mountpoint normalization should add a leading slash");
  ctx.Expect(universal_gnss_ntrip::NormalizeMountpointPath("/MOUNT") == "/MOUNT",
             "mountpoint normalization should preserve a normalized path");
  ctx.Expect(universal_gnss_ntrip::NormalizeMountpointPath("//nested/mount") == "/nested/mount",
             "mountpoint normalization should collapse repeated leading slashes");
  ctx.Expect(universal_gnss_ntrip::NormalizeMountpointPath("").empty() == false &&
                 universal_gnss_ntrip::NormalizeMountpointPath("") == "/",
             "empty mountpoints should normalize to the caster root");
}

void TestNtripV1RequestFormatting(TestContext& ctx)
{
  universal_gnss_ntrip::NtripConfig config;
  config.host = "caster.example.com";
  config.port = 2101u;
  config.mountpoint = "RTCM32";
  config.username = "user";
  config.password = "pass";
  config.version = universal_gnss_ntrip::NtripVersion::kV1;
  config.user_agent = "universal-gnss-test";

  const auto request = universal_gnss_ntrip::BuildNtripGetRequest(config);
  ctx.Expect(request.mountpoint_path == "/RTCM32",
             "built request should expose the normalized mountpoint path");
  ctx.Expect(request.includes_authorization && !request.includes_ntrip_version_header,
             "NTRIP v1 requests should include auth when present and omit the v2 header");
  ctx.Expect(request.request_text.find("GET /RTCM32 HTTP/1.0\r\n") == 0,
             "NTRIP v1 requests should use HTTP/1.0");
  ctx.Expect(request.request_text.find("User-Agent: NTRIP universal-gnss-test\r\n") !=
                 std::string::npos,
             "NTRIP v1 requests should include the configured user agent");
  ctx.Expect(request.request_text.find("Host: ") == std::string::npos &&
                 request.request_text.find("Ntrip-Version: ") == std::string::npos,
             "NTRIP v1 requests should not emit v2-only headers");
}

void TestNtripV2RequestFormatting(TestContext& ctx)
{
  universal_gnss_ntrip::NtripConfig config;
  config.host = "caster.example.com";
  config.port = 2201u;
  config.mountpoint = "/MYMOUNT";
  config.version = universal_gnss_ntrip::NtripVersion::kV2;
  config.user_agent.clear();

  const auto request = universal_gnss_ntrip::BuildNtripGetRequest(config);
  ctx.Expect(!request.includes_authorization && request.includes_ntrip_version_header,
             "NTRIP v2 requests should advertise the v2 header and omit empty auth");
  ctx.Expect(request.request_text.find("GET /MYMOUNT HTTP/1.1\r\n") == 0,
             "NTRIP v2 requests should use HTTP/1.1");
  ctx.Expect(request.request_text.find("Host: caster.example.com:2201\r\n") !=
                 std::string::npos,
             "NTRIP v2 requests should include the host header");
  ctx.Expect(request.request_text.find("Ntrip-Version: Ntrip/2.0\r\n") != std::string::npos,
             "NTRIP v2 requests should include the Ntrip-Version header");
  ctx.Expect(request.request_text.find("User-Agent: NTRIP universal-gnss\r\n") !=
                 std::string::npos,
             "empty user-agent configuration should fall back to the default");
  ctx.Expect(request.request_text.find("Authorization: ") == std::string::npos,
             "empty credentials should not emit an Authorization header");
}

void TestGgaPolicyDefaults(TestContext& ctx)
{
  universal_gnss_ntrip::NtripConfig config;
  const auto default_policy = universal_gnss_ntrip::BuildGgaInjectionPolicy(config);

  ctx.Expect(!default_policy.enabled &&
                 default_policy.interval_s == 10u &&
                 default_policy.source_position_requirement ==
                     universal_gnss_ntrip::GgaSourcePositionRequirement::kRequirePositionFix &&
                 !default_policy.last_sent_timestamp_ns.has_value(),
             "default GGA policy should stay disabled and require a runtime position when enabled");
  ctx.Expect(config.reconnect_policy.enabled &&
                 config.reconnect_policy.initial_delay_ms == 1000u &&
                 config.reconnect_policy.max_delay_ms == 30000u &&
                 config.reconnect_policy.multiplier == 2.0 &&
                 !config.reconnect_policy.max_attempts.has_value() &&
                 config.reconnect_policy.reset_after_success,
             "default reconnect policy should expose a portable retry foundation with conservative defaults");
  ctx.Expect(!universal_gnss_ntrip::ShouldInjectGga(
                 default_policy,
                 true,
                 std::int64_t{1000000000LL}),
             "disabled GGA policy should not request injection");

  config.send_gga = true;
  config.gga_interval_s = 5u;
  auto enabled_policy = universal_gnss_ntrip::BuildGgaInjectionPolicy(config);
  ctx.Expect(universal_gnss_ntrip::ShouldInjectGga(
                 enabled_policy,
                 true,
                 std::int64_t{1000000000LL}),
             "enabled policy with a valid position should allow the first injection");
  universal_gnss_ntrip::MarkGgaInjected(enabled_policy, 1000000000LL);
  ctx.Expect(!universal_gnss_ntrip::ShouldInjectGga(
                 enabled_policy,
                 true,
                 std::int64_t{4000000000LL}),
             "GGA policy should enforce the configured injection interval");
  ctx.Expect(universal_gnss_ntrip::ShouldInjectGga(
                 enabled_policy,
                 true,
                 std::int64_t{6000000000LL}),
             "GGA policy should allow injection again after the interval elapses");
}

void TestMetricsModel(TestContext& ctx)
{
  universal_gnss_ntrip::NtripConnectionMetrics metrics;
  ctx.Expect(metrics.bytes_received == 0u &&
                 metrics.bytes_sent == 0u &&
                 metrics.rtcm_frames_seen == 0u &&
                 metrics.rtcm_frames_received == 0u &&
                 metrics.invalid_rtcm_frames == 0u &&
                 metrics.gga_sent_count == 0u &&
                 metrics.gga_send_errors == 0u &&
                 !metrics.last_gga_sent_timestamp_ns.has_value() &&
                 !metrics.last_gga_error.has_value() &&
                 !metrics.connected &&
                 !metrics.request_sent &&
                 !metrics.response_received &&
                 metrics.reconnect_count == 0u &&
                 metrics.last_error == universal_gnss_ntrip::NtripClientError::kNone,
             "default metrics should start empty and disconnected");

  universal_gnss_ntrip::MarkConnected(metrics);
  universal_gnss_ntrip::NoteReceivedBytes(metrics, 120u);
  universal_gnss_ntrip::NoteSentBytes(metrics, 64u);
  universal_gnss_ntrip::MarkRequestSent(metrics);
  universal_gnss_ntrip::MarkResponseReceived(metrics);
  universal_gnss_ntrip::NoteRtcmFrame(metrics, 1005u, true);
  universal_gnss_ntrip::NoteRtcmFrame(metrics, std::nullopt, false);
  universal_gnss_ntrip::NoteGgaSent(metrics, 123456789LL);
  universal_gnss_ntrip::NoteGgaSendError(
      metrics,
      universal_gnss_ntrip::NtripGgaSendError::kGenerationFailed);
  universal_gnss_ntrip::NoteReconnect(metrics);
  universal_gnss_ntrip::MarkDisconnected(
      metrics,
      universal_gnss_ntrip::NtripClientError::kTimeout);

  ctx.Expect(metrics.bytes_received == 120u &&
                 metrics.bytes_sent == 64u &&
                 metrics.rtcm_frames_seen == 2u &&
                 metrics.rtcm_frames_received == 1u &&
                 metrics.invalid_rtcm_frames == 1u &&
                 metrics.gga_sent_count == 1u &&
                 metrics.gga_send_errors == 1u &&
                 metrics.last_gga_sent_timestamp_ns == std::optional<std::int64_t>(123456789LL) &&
                 metrics.last_gga_error ==
                     std::optional<universal_gnss_ntrip::NtripGgaSendError>(
                         universal_gnss_ntrip::NtripGgaSendError::kGenerationFailed) &&
                 metrics.last_rtcm_message_type == 1005u &&
                 metrics.request_sent &&
                 metrics.response_received,
             "metrics helpers should track request/response state, RTCM frame counts, and GGA activity");
  ctx.Expect(!metrics.connected &&
                 metrics.reconnect_count == 1u &&
                 metrics.last_error == universal_gnss_ntrip::NtripClientError::kTimeout,
             "metrics helpers should track reconnects and the last disconnect reason");

  universal_gnss_ntrip::ClearLastError(metrics);
  universal_gnss_ntrip::ClearLastGgaError(metrics);
  ctx.Expect(metrics.last_error == universal_gnss_ntrip::NtripClientError::kNone,
             "metrics should allow clearing the last error");
  ctx.Expect(!metrics.last_gga_error.has_value(),
             "metrics should allow clearing the last GGA error");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestBasicAuthorization(ctx);
  TestMountpointNormalization(ctx);
  TestNtripV1RequestFormatting(ctx);
  TestNtripV2RequestFormatting(ctx);
  TestGgaPolicyDefaults(ctx);
  TestMetricsModel(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_ntrip foundation tests passed\n";
  return EXIT_SUCCESS;
}
