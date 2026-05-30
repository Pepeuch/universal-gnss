#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_health.hpp"

namespace
{

using universal_gnss::CombineDiagnosticSeverities;
using universal_gnss::ComputeOverallDiagnosticSeverity;
using universal_gnss::GnssDiagnosticCategory;
using universal_gnss::GnssDiagnosticEvent;
using universal_gnss::GnssDiagnosticEvents;
using universal_gnss::GnssDiagnosticSeverity;
using universal_gnss::GnssHealthSummary;

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

void TestSeverityOrdering(TestContext& ctx)
{
  ctx.Expect(CombineDiagnosticSeverities(GnssDiagnosticSeverity::kOk,
                                         GnssDiagnosticSeverity::kInfo) ==
                 GnssDiagnosticSeverity::kInfo,
             "info should override ok");
  ctx.Expect(CombineDiagnosticSeverities(GnssDiagnosticSeverity::kWarning,
                                         GnssDiagnosticSeverity::kInfo) ==
                 GnssDiagnosticSeverity::kWarning,
             "warning should override info");
  ctx.Expect(CombineDiagnosticSeverities(GnssDiagnosticSeverity::kStale,
                                         GnssDiagnosticSeverity::kWarning) ==
                 GnssDiagnosticSeverity::kStale,
             "stale should outrank warning");
  ctx.Expect(CombineDiagnosticSeverities(GnssDiagnosticSeverity::kError,
                                         GnssDiagnosticSeverity::kUnknown) ==
                 GnssDiagnosticSeverity::kError,
             "error should outrank unknown");
  ctx.Expect(CombineDiagnosticSeverities(GnssDiagnosticSeverity::kUnknown,
                                         GnssDiagnosticSeverity::kOk) ==
                 GnssDiagnosticSeverity::kUnknown,
             "unknown should outrank ok");
}

void TestEventCreation(TestContext& ctx)
{
  GnssDiagnosticEvent event;
  event.severity = GnssDiagnosticSeverity::kWarning;
  event.category = GnssDiagnosticCategory::kParser;
  event.code = "nmea.checksum_mismatch";
  event.message = "NMEA checksum mismatch";
  event.timestamp_ns = 123456789;
  event.source = std::string("nmea");

  ctx.Expect(event.severity == GnssDiagnosticSeverity::kWarning,
             "event should keep its severity");
  ctx.Expect(event.category == GnssDiagnosticCategory::kParser,
             "event should keep its category");
  ctx.Expect(event.code == "nmea.checksum_mismatch",
             "event should keep its portable code id");
  ctx.Expect(event.message == "NMEA checksum mismatch",
             "event should keep its message");
  ctx.Expect(event.timestamp_ns == std::optional<universal_gnss::GnssTimestampNs>(123456789),
             "event should keep its optional timestamp");
  ctx.Expect(event.source == std::optional<std::string>("nmea"),
             "event should keep its optional source");
}

void TestOverallSeverityComputation(TestContext& ctx)
{
  GnssDiagnosticEvents events;
  events.push_back({GnssDiagnosticSeverity::kInfo,
                    GnssDiagnosticCategory::kRuntime,
                    "runtime.started",
                    "Runtime started"});
  events.push_back({GnssDiagnosticSeverity::kWarning,
                    GnssDiagnosticCategory::kCorrection,
                    "correction.age_high",
                    "Correction age is elevated"});
  events.push_back({GnssDiagnosticSeverity::kStale,
                    GnssDiagnosticCategory::kTiming,
                    "runtime.stale",
                    "Runtime data is stale"});

  ctx.Expect(ComputeOverallDiagnosticSeverity(events) == GnssDiagnosticSeverity::kStale,
             "overall severity should match the most severe event");
  events.push_back({GnssDiagnosticSeverity::kUnknown,
                    GnssDiagnosticCategory::kRuntime,
                    "runtime.unknown",
                    "Runtime health is unknown"});
  ctx.Expect(ComputeOverallDiagnosticSeverity(events) == GnssDiagnosticSeverity::kStale,
             "stale should continue to outrank unknown when both are present");
  ctx.Expect(ComputeOverallDiagnosticSeverity(GnssDiagnosticEvents{}) ==
                 GnssDiagnosticSeverity::kOk,
             "empty event lists should fold to ok");
}

void TestWarningAndErrorDetection(TestContext& ctx)
{
  GnssHealthSummary summary;
  summary.AddEvent({GnssDiagnosticSeverity::kInfo,
                    GnssDiagnosticCategory::kRuntime,
                    "runtime.started",
                    "Runtime started"});

  ctx.Expect(!summary.HasWarnings(), "info-only summaries should not report warnings");
  ctx.Expect(!summary.HasErrors(), "info-only summaries should not report errors");

  summary.AddEvent({GnssDiagnosticSeverity::kWarning,
                    GnssDiagnosticCategory::kTransport,
                    "transport.retrying",
                    "Transport is retrying"});
  ctx.Expect(summary.HasWarnings(), "warning events should be detected");
  ctx.Expect(!summary.HasErrors(), "warning events should not imply errors");

  summary.AddEvent({GnssDiagnosticSeverity::kError,
                    GnssDiagnosticCategory::kReceiver,
                    "receiver.unresponsive",
                    "Receiver did not answer"});
  ctx.Expect(summary.HasWarnings(), "error events should still count as a non-ok condition");
  ctx.Expect(summary.HasErrors(), "error events should be detected");
  ctx.Expect(summary.overall_severity == GnssDiagnosticSeverity::kError,
             "summary severity should track the strongest added event");
}

void TestClearResetBehavior(TestContext& ctx)
{
  GnssHealthSummary summary;
  summary.fix_valid = true;
  summary.rtk_available = true;
  summary.correction_available = true;
  summary.receiver_healthy = true;
  summary.transport_healthy = true;
  summary.parser_healthy = true;
  summary.stale_data = true;
  summary.AddEvent({GnssDiagnosticSeverity::kWarning,
                    GnssDiagnosticCategory::kConfiguration,
                    "config.partial",
                    "Receiver config only partially applied"});

  summary.Clear();

  ctx.Expect(summary.overall_severity == GnssDiagnosticSeverity::kUnknown,
             "clear should reset the overall severity to the default unknown state");
  ctx.Expect(!summary.fix_valid && !summary.rtk_available && !summary.correction_available,
             "clear should reset runtime availability flags");
  ctx.Expect(!summary.receiver_healthy && !summary.transport_healthy && !summary.parser_healthy,
             "clear should reset per-layer health flags");
  ctx.Expect(!summary.stale_data, "clear should reset stale tracking");
  ctx.Expect(summary.events.empty(), "clear should remove accumulated events");
}

void TestStaleEventBehavior(TestContext& ctx)
{
  GnssHealthSummary summary;
  summary.AddEvent({GnssDiagnosticSeverity::kStale,
                    GnssDiagnosticCategory::kTiming,
                    "runtime.sample_stale",
                    "Latest sample is stale",
                    1000,
                    std::string("receiver_session")});

  ctx.Expect(summary.stale_data, "stale events should mark the summary as stale");
  ctx.Expect(summary.HasWarnings(), "stale events should count as a non-ok condition");
  ctx.Expect(!summary.HasErrors(), "stale events should not count as errors");
  ctx.Expect(summary.overall_severity == GnssDiagnosticSeverity::kStale,
             "stale events should drive the overall severity when they are the strongest issue");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestSeverityOrdering(ctx);
  TestEventCreation(ctx);
  TestOverallSeverityComputation(ctx);
  TestWarningAndErrorDetection(ctx);
  TestClearResetBehavior(ctx);
  TestStaleEventBehavior(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_core diagnostics tests passed\n";
  return EXIT_SUCCESS;
}
