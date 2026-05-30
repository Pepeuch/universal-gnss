#pragma once

#include <cstdint>
#include <optional>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_ntrip/gga_injection_policy.hpp"
#include "universal_gnss_ntrip/gga_sentence_builder.hpp"
#include "universal_gnss_transport/byte_stream.hpp"

namespace universal_gnss_ntrip
{

struct GgaInjectorConfig
{
  GgaInjectionPolicy policy{};
  GgaSentenceBuilderOptions sentence_builder_options{};
};

struct GgaInjectorMetrics
{
  std::uint64_t attempts{0u};
  std::uint64_t sentences_built{0u};
  std::uint64_t sentences_sent{0u};
  std::uint64_t skipped_disabled{0u};
  std::uint64_t skipped_interval{0u};
  std::uint64_t skipped_missing_position{0u};
  std::uint64_t skipped_position_required{0u};
  std::uint64_t write_errors{0u};
  std::optional<GgaSentenceBuildError> last_build_error{};
  std::optional<universal_gnss_transport::TransportError> last_write_error{};
};

enum class GgaInjectionStatus : std::uint8_t
{
  kSent = 0,
  kSkippedDisabled = 1,
  kSkippedInterval = 2,
  kSkippedMissingPosition = 3,
  kSkippedPositionRequired = 4,
  kBuildError = 5,
  kWriteError = 6,
};

struct GgaInjectionResult
{
  GgaInjectionStatus status{GgaInjectionStatus::kBuildError};
  std::optional<GgaSentenceBuildError> build_error{};
  std::optional<universal_gnss_transport::TransportError> write_error{};

  bool sent() const;
  bool skipped() const;
  bool ok() const;
};

class GgaInjector
{
public:
  GgaInjector() = default;
  explicit GgaInjector(GgaInjectorConfig config);

  void set_config(GgaInjectorConfig config);
  const GgaInjectorConfig& config() const;
  const GgaInjectionPolicy& policy() const;
  const GgaInjectorMetrics& metrics() const;

  GgaInjectionResult MaybeInject(universal_gnss_transport::ByteSink& sink,
                                 const universal_gnss::GnssRuntimeState& state,
                                 universal_gnss::GnssTimestampNs now_timestamp_ns);

  void Reset();

private:
  GgaInjectionResult BuildAndWriteSentence(universal_gnss_transport::ByteSink& sink,
                                           const universal_gnss::GnssRuntimeState& state,
                                           universal_gnss::GnssTimestampNs now_timestamp_ns);

  GgaInjectorConfig config_{};
  GgaInjectorMetrics metrics_{};
};

}  // namespace universal_gnss_ntrip
