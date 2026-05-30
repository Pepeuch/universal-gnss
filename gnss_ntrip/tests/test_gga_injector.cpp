#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_ntrip/gga_injector.hpp"
#include "universal_gnss_transport/memory_stream.hpp"

namespace
{

using universal_gnss::GnssFixType;
using universal_gnss::GnssRuntimeState;
using universal_gnss_ntrip::BuildNmeaGgaSentence;
using universal_gnss_ntrip::GgaInjectionStatus;
using universal_gnss_ntrip::GgaInjector;
using universal_gnss_ntrip::GgaInjectorConfig;
using universal_gnss_ntrip::GgaSentenceTalker;
using universal_gnss_transport::MemoryByteSink;
using universal_gnss_transport::TransportError;

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

GnssRuntimeState MakeRuntimeState()
{
  GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = GnssFixType::kFix;
  state.latitude_deg = 48.1173;
  state.longitude_deg = 11.5166667;
  state.altitude_m = 545.4;
  state.hdop = 0.9f;
  state.satellites_used = 8u;
  return state;
}

std::string SinkText(const MemoryByteSink& sink)
{
  return std::string(sink.written_bytes().begin(), sink.written_bytes().end());
}

void TestDisabledPolicySkips(TestContext& ctx)
{
  GgaInjector injector;
  MemoryByteSink sink;

  const auto result = injector.MaybeInject(sink, MakeRuntimeState(), 1000000000LL);
  ctx.Expect(result.status == GgaInjectionStatus::kSkippedDisabled && result.skipped(),
             "disabled policy should skip injection");
  ctx.Expect(injector.metrics().attempts == 1u &&
                 injector.metrics().skipped_disabled == 1u &&
                 injector.metrics().sentences_sent == 0u &&
                 sink.written_bytes().empty(),
             "disabled policy should update only skip counters");
}

void TestFirstSendAndIntervalBehavior(TestContext& ctx)
{
  GgaInjectorConfig config;
  config.policy.enabled = true;
  config.policy.interval_s = 5u;
  GgaInjector injector(config);
  MemoryByteSink sink;

  const auto first = injector.MaybeInject(sink, MakeRuntimeState(), 1000000000LL);
  ctx.Expect(first.status == GgaInjectionStatus::kSent && first.sent(),
             "first eligible call should send a GGA sentence");
  ctx.Expect(injector.metrics().sentences_built == 1u &&
                 injector.metrics().sentences_sent == 1u &&
                 injector.policy().last_sent_timestamp_ns ==
                     std::optional<std::int64_t>(1000000000LL),
             "successful send should update build/send counters and last-sent timestamp");

  const std::size_t first_size = sink.written_bytes().size();
  const auto second = injector.MaybeInject(sink, MakeRuntimeState(), 4000000000LL);
  ctx.Expect(second.status == GgaInjectionStatus::kSkippedInterval &&
                 injector.metrics().skipped_interval == 1u &&
                 sink.written_bytes().size() == first_size,
             "calls before the interval elapses should skip without writing");

  const auto third = injector.MaybeInject(sink, MakeRuntimeState(), 7000000000LL);
  ctx.Expect(third.status == GgaInjectionStatus::kSent &&
                 injector.metrics().sentences_sent == 2u &&
                 injector.policy().last_sent_timestamp_ns ==
                     std::optional<std::int64_t>(7000000000LL),
             "calls after the interval elapses should send again");
}

void TestMissingPositionAndRequiredFixSkips(TestContext& ctx)
{
  GgaInjectorConfig config;
  config.policy.enabled = true;
  GgaInjector injector(config);
  MemoryByteSink sink;

  auto missing_position = MakeRuntimeState();
  missing_position.longitude_deg.reset();

  const auto missing_result = injector.MaybeInject(sink, missing_position, 1000000000LL);
  ctx.Expect(missing_result.status == GgaInjectionStatus::kSkippedMissingPosition &&
                 injector.metrics().skipped_missing_position == 1u &&
                 injector.metrics().sentences_sent == 0u,
             "missing coordinates should skip explicit-call GGA injection");

  auto no_fix = MakeRuntimeState();
  no_fix.fix_valid = false;
  no_fix.fix_type = GnssFixType::kNoFix;

  const auto no_fix_result = injector.MaybeInject(sink, no_fix, 2000000000LL);
  ctx.Expect(no_fix_result.status == GgaInjectionStatus::kSkippedPositionRequired &&
                 injector.metrics().skipped_position_required == 1u &&
                 injector.metrics().sentences_sent == 0u,
             "require-position-fix policy should skip when fix_valid is false");
}

void TestNoFixAllowedWhenPolicyAllowsIt(TestContext& ctx)
{
  GgaInjectorConfig config;
  config.policy.enabled = true;
  config.policy.source_position_requirement =
      universal_gnss_ntrip::GgaSourcePositionRequirement::kNone;
  config.sentence_builder_options.talker = GgaSentenceTalker::kGn;
  GgaInjector injector(config);
  MemoryByteSink sink;

  auto no_fix = MakeRuntimeState();
  no_fix.fix_valid = false;
  no_fix.fix_type = GnssFixType::kNoFix;

  const auto result = injector.MaybeInject(sink, no_fix, 1000000000LL);
  const std::string text = SinkText(sink);
  ctx.Expect(result.status == GgaInjectionStatus::kSent &&
                 injector.metrics().sentences_sent == 1u &&
                 text.find("$GNGGA,000000.00,") == 0 &&
                 text.find(",0,08,0.9,545.4,M,,,,*") != std::string::npos,
             "policies that do not require a fix should allow no-fix GGA injection when coordinates are present");
}

void TestWriteErrorDoesNotAdvanceLastSent(TestContext& ctx)
{
  GgaInjectorConfig config;
  config.policy.enabled = true;
  GgaInjector injector(config);
  MemoryByteSink sink;
  sink.InjectNextWriteError(TransportError::kWriteFailure);

  const auto result = injector.MaybeInject(sink, MakeRuntimeState(), 1000000000LL);
  ctx.Expect(result.status == GgaInjectionStatus::kWriteError &&
                 result.write_error ==
                     std::optional<TransportError>(TransportError::kWriteFailure) &&
                 injector.metrics().sentences_built == 1u &&
                 injector.metrics().sentences_sent == 0u &&
                 injector.metrics().write_errors == 1u &&
                 injector.metrics().last_write_error ==
                     std::optional<TransportError>(TransportError::kWriteFailure) &&
                 !injector.policy().last_sent_timestamp_ns.has_value(),
             "write failures should increment error counters without advancing last-sent time");
}

void TestSentenceContainsChecksumAndCrLf(TestContext& ctx)
{
  GgaInjectorConfig config;
  config.policy.enabled = true;
  GgaInjector injector(config);
  MemoryByteSink sink;

  const auto result = injector.MaybeInject(sink, MakeRuntimeState(), 1000000000LL);
  const std::string text = SinkText(sink);
  const auto expected =
      BuildNmeaGgaSentence(MakeRuntimeState(), config.sentence_builder_options).sentence;

  ctx.Expect(result.status == GgaInjectionStatus::kSent &&
                 text == expected &&
                 text.find('*') != std::string::npos &&
                 text.size() >= 2u &&
                 text.substr(text.size() - 2u) == "\r\n",
             "successful injection should write the complete checksum-protected CRLF-terminated sentence");
}

void TestResetClearsMetricsAndLastSent(TestContext& ctx)
{
  GgaInjectorConfig config;
  config.policy.enabled = true;
  GgaInjector injector(config);
  MemoryByteSink sink;

  injector.MaybeInject(sink, MakeRuntimeState(), 1000000000LL);
  injector.Reset();

  ctx.Expect(injector.metrics().attempts == 0u &&
                 injector.metrics().sentences_built == 0u &&
                 injector.metrics().sentences_sent == 0u &&
                 injector.metrics().write_errors == 0u &&
                 !injector.policy().last_sent_timestamp_ns.has_value(),
             "reset should clear counters and the last-sent timestamp");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestDisabledPolicySkips(ctx);
  TestFirstSendAndIntervalBehavior(ctx);
  TestMissingPositionAndRequiredFixSkips(ctx);
  TestNoFixAllowedWhenPolicyAllowsIt(ctx);
  TestWriteErrorDoesNotAdvanceLastSent(ctx);
  TestSentenceContainsChecksumAndCrLf(ctx);
  TestResetClearsMetricsAndLastSent(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_ntrip GGA injector tests passed\n";
  return EXIT_SUCCESS;
}
