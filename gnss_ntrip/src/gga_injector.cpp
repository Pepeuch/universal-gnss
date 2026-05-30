#include "universal_gnss_ntrip/gga_injector.hpp"

#include <cstddef>
#include <cstdint>

namespace universal_gnss_ntrip
{

namespace
{

bool HasCoordinates(const universal_gnss::GnssRuntimeState& state)
{
  return state.latitude_deg.has_value() && state.longitude_deg.has_value();
}

}  // namespace

bool GgaInjectionResult::sent() const
{
  return status == GgaInjectionStatus::kSent;
}

bool GgaInjectionResult::skipped() const
{
  return status == GgaInjectionStatus::kSkippedDisabled ||
         status == GgaInjectionStatus::kSkippedInterval ||
         status == GgaInjectionStatus::kSkippedMissingPosition ||
         status == GgaInjectionStatus::kSkippedPositionRequired;
}

bool GgaInjectionResult::ok() const
{
  return status != GgaInjectionStatus::kBuildError &&
         status != GgaInjectionStatus::kWriteError;
}

GgaInjector::GgaInjector(GgaInjectorConfig config)
    : config_(std::move(config))
{
}

void GgaInjector::set_config(GgaInjectorConfig config)
{
  config_ = std::move(config);
}

const GgaInjectorConfig& GgaInjector::config() const
{
  return config_;
}

const GgaInjectionPolicy& GgaInjector::policy() const
{
  return config_.policy;
}

const GgaInjectorMetrics& GgaInjector::metrics() const
{
  return metrics_;
}

GgaInjectionResult GgaInjector::MaybeInject(universal_gnss_transport::ByteSink& sink,
                                            const universal_gnss::GnssRuntimeState& state,
                                            const universal_gnss::GnssTimestampNs now_timestamp_ns)
{
  ++metrics_.attempts;
  metrics_.last_build_error.reset();
  metrics_.last_write_error.reset();

  if (!config_.policy.enabled)
  {
    ++metrics_.skipped_disabled;
    return {GgaInjectionStatus::kSkippedDisabled, std::nullopt, std::nullopt};
  }

  if (!HasCoordinates(state))
  {
    ++metrics_.skipped_missing_position;
    return {GgaInjectionStatus::kSkippedMissingPosition, std::nullopt, std::nullopt};
  }

  if (config_.policy.source_position_requirement ==
          GgaSourcePositionRequirement::kRequirePositionFix &&
      !state.fix_valid)
  {
    ++metrics_.skipped_position_required;
    return {GgaInjectionStatus::kSkippedPositionRequired, std::nullopt, std::nullopt};
  }

  if (!ShouldInjectGga(config_.policy, state.fix_valid, now_timestamp_ns))
  {
    ++metrics_.skipped_interval;
    return {GgaInjectionStatus::kSkippedInterval, std::nullopt, std::nullopt};
  }

  return BuildAndWriteSentence(sink, state, now_timestamp_ns);
}

void GgaInjector::Reset()
{
  metrics_ = GgaInjectorMetrics{};
  config_.policy.last_sent_timestamp_ns.reset();
}

GgaInjectionResult GgaInjector::BuildAndWriteSentence(
    universal_gnss_transport::ByteSink& sink,
    const universal_gnss::GnssRuntimeState& state,
    const universal_gnss::GnssTimestampNs now_timestamp_ns)
{
  const auto built = BuildNmeaGgaSentence(state, config_.sentence_builder_options);
  if (!built.ok())
  {
    metrics_.last_build_error = built.error;
    return {GgaInjectionStatus::kBuildError, built.error, std::nullopt};
  }

  ++metrics_.sentences_built;

  std::size_t offset = 0u;
  while (offset < built.sentence.size())
  {
    const auto write_result = sink.Write(
        reinterpret_cast<const std::uint8_t*>(built.sentence.data()) +
            static_cast<std::ptrdiff_t>(offset),
        built.sentence.size() - offset);

    if (write_result.status != universal_gnss_transport::TransportStatus::kOk)
    {
      ++metrics_.write_errors;
      metrics_.last_write_error = write_result.error;
      return {GgaInjectionStatus::kWriteError, std::nullopt, write_result.error};
    }

    if (write_result.bytes_written == 0u)
    {
      ++metrics_.write_errors;
      metrics_.last_write_error = universal_gnss_transport::TransportError::kWriteFailure;
      return {GgaInjectionStatus::kWriteError,
              std::nullopt,
              universal_gnss_transport::TransportError::kWriteFailure};
    }

    offset += write_result.bytes_written;
  }

  ++metrics_.sentences_sent;
  MarkGgaInjected(config_.policy, now_timestamp_ns);
  return {GgaInjectionStatus::kSent, std::nullopt, std::nullopt};
}

}  // namespace universal_gnss_ntrip
