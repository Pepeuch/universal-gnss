#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "universal_gnss/gnss_runtime_state.hpp"

namespace universal_gnss_driver
{

enum class ReceiverEpochDomain : std::uint8_t
{
  kUnknown = 0,
  kUtcNanosecondsOfDay = 1,
  kGpsTowMilliseconds = 2,
  kGpsWeekTowMilliseconds = 3,
  kReceiverWeekTowMilliseconds = 4,
};

enum class ReceiverEpochRelation : std::uint8_t
{
  kUnavailable = 0,
  kFirst = 1,
  kAdvanced = 2,
  kIdentical = 3,
  kRegressed = 4,
  kDomainChanged = 5,
};

struct ReceiverEpoch
{
  ReceiverEpochDomain domain{ReceiverEpochDomain::kUnknown};
  std::uint64_t value{0u};
  std::optional<std::uint64_t> rollover_period{};
};

struct PositionPayloadFreshnessMetrics
{
  std::size_t valid_position_payload_observations{0u};
  std::size_t invalid_position_observations{0u};
  std::size_t position_payload_changes{0u};
  std::size_t consecutive_identical_position_observations{0u};
  std::optional<universal_gnss::GnssTimestampNs> last_position_observation_timestamp_ns{};
  std::optional<universal_gnss::GnssTimestampNs> last_position_payload_change_timestamp_ns{};
  std::optional<universal_gnss::GnssTimestampNs> unchanged_observation_span_ns{};

  std::size_t receiver_epoch_observations{0u};
  std::size_t receiver_epoch_advances{0u};
  std::size_t consecutive_identical_receiver_epochs{0u};
  std::size_t receiver_epoch_regressions{0u};
  ReceiverEpochRelation last_receiver_epoch_relation{ReceiverEpochRelation::kUnavailable};
  std::optional<ReceiverEpoch> last_receiver_epoch{};
};

inline std::optional<ReceiverEpoch>
MakeUtcTimeOfDayEpoch(const std::uint8_t hour, const std::uint8_t minute, const double second)
{
  if (hour > 23u || minute > 59u || !std::isfinite(second) || second < 0.0 || second >= 61.0)
  {
    return std::nullopt;
  }

  constexpr std::uint64_t kNanosecondsPerSecond = 1000000000u;
  constexpr std::uint64_t kSecondsPerDay = 86400u;
  const auto second_ns = static_cast<std::uint64_t>(std::llround(second * kNanosecondsPerSecond));
  return ReceiverEpoch{
      ReceiverEpochDomain::kUtcNanosecondsOfDay,
      (static_cast<std::uint64_t>(hour) * 3600u + static_cast<std::uint64_t>(minute) * 60u) *
              kNanosecondsPerSecond +
          second_ns,
      kSecondsPerDay * kNanosecondsPerSecond};
}

inline ReceiverEpoch MakeGpsTowEpoch(const std::uint32_t tow_ms)
{
  constexpr std::uint64_t kMillisecondsPerWeek = 604800000u;
  return ReceiverEpoch{ReceiverEpochDomain::kGpsTowMilliseconds, tow_ms, kMillisecondsPerWeek};
}

inline ReceiverEpoch MakeGpsWeekTowEpoch(const std::uint16_t week, const std::uint32_t tow_ms)
{
  constexpr std::uint64_t kMillisecondsPerWeek = 604800000u;
  return ReceiverEpoch{ReceiverEpochDomain::kGpsWeekTowMilliseconds,
                       static_cast<std::uint64_t>(week) * kMillisecondsPerWeek + tow_ms,
                       std::nullopt};
}

inline ReceiverEpoch MakeReceiverWeekTowEpoch(const std::uint16_t week, const std::uint32_t tow_ms)
{
  constexpr std::uint64_t kMillisecondsPerWeek = 604800000u;
  return ReceiverEpoch{ReceiverEpochDomain::kReceiverWeekTowMilliseconds,
                       static_cast<std::uint64_t>(week) * kMillisecondsPerWeek + tow_ms,
                       std::nullopt};
}

class PositionPayloadFreshnessTracker
{
public:
  void Observe(const universal_gnss::GnssRuntimeState& observation,
               const std::optional<ReceiverEpoch>& receiver_epoch = std::nullopt)
  {
    metrics_.last_position_observation_timestamp_ns = observation.timestamp_ns;
    ObserveReceiverEpoch(receiver_epoch);

    if (!observation.fix_valid || !observation.latitude_deg.has_value() ||
        !observation.longitude_deg.has_value() || !std::isfinite(*observation.latitude_deg) ||
        !std::isfinite(*observation.longitude_deg))
    {
      ++metrics_.invalid_position_observations;
      comparable_position_.reset();
      metrics_.consecutive_identical_position_observations = 0u;
      metrics_.unchanged_observation_span_ns.reset();
      return;
    }

    ++metrics_.valid_position_payload_observations;
    const Position current{*observation.latitude_deg, *observation.longitude_deg};
    if (comparable_position_.has_value() &&
        current.latitude_deg == comparable_position_->latitude_deg &&
        current.longitude_deg == comparable_position_->longitude_deg)
    {
      ++metrics_.consecutive_identical_position_observations;
      UpdateUnchangedObservationSpan(observation.timestamp_ns);
      return;
    }

    comparable_position_ = current;
    ++metrics_.position_payload_changes;
    metrics_.consecutive_identical_position_observations = 1u;
    metrics_.last_position_payload_change_timestamp_ns = observation.timestamp_ns;
    metrics_.unchanged_observation_span_ns = observation.timestamp_ns.has_value()
                                                 ? std::optional<universal_gnss::GnssTimestampNs>(0)
                                                 : std::nullopt;
  }

  void Reset()
  {
    metrics_ = PositionPayloadFreshnessMetrics{};
    comparable_position_.reset();
  }

  const PositionPayloadFreshnessMetrics& metrics() const
  {
    return metrics_;
  }

private:
  struct Position
  {
    double latitude_deg{0.0};
    double longitude_deg{0.0};
  };

  void UpdateUnchangedObservationSpan(
      const std::optional<universal_gnss::GnssTimestampNs>& observation_timestamp_ns)
  {
    if (!observation_timestamp_ns.has_value() ||
        !metrics_.last_position_payload_change_timestamp_ns.has_value() ||
        *observation_timestamp_ns < *metrics_.last_position_payload_change_timestamp_ns)
    {
      metrics_.unchanged_observation_span_ns.reset();
      return;
    }

    metrics_.unchanged_observation_span_ns =
        *observation_timestamp_ns - *metrics_.last_position_payload_change_timestamp_ns;
  }

  void ObserveReceiverEpoch(const std::optional<ReceiverEpoch>& receiver_epoch)
  {
    if (!receiver_epoch.has_value() || receiver_epoch->domain == ReceiverEpochDomain::kUnknown)
    {
      metrics_.last_receiver_epoch_relation = ReceiverEpochRelation::kUnavailable;
      return;
    }

    ++metrics_.receiver_epoch_observations;
    if (!metrics_.last_receiver_epoch.has_value())
    {
      metrics_.last_receiver_epoch = receiver_epoch;
      metrics_.last_receiver_epoch_relation = ReceiverEpochRelation::kFirst;
      metrics_.consecutive_identical_receiver_epochs = 1u;
      return;
    }

    const ReceiverEpoch previous = *metrics_.last_receiver_epoch;
    metrics_.last_receiver_epoch = receiver_epoch;
    if (previous.domain != receiver_epoch->domain)
    {
      metrics_.last_receiver_epoch_relation = ReceiverEpochRelation::kDomainChanged;
      metrics_.consecutive_identical_receiver_epochs = 1u;
      return;
    }

    if (receiver_epoch->value == previous.value)
    {
      metrics_.last_receiver_epoch_relation = ReceiverEpochRelation::kIdentical;
      ++metrics_.consecutive_identical_receiver_epochs;
      return;
    }

    bool advanced = receiver_epoch->value > previous.value;
    if (!advanced && receiver_epoch->rollover_period.has_value() &&
        previous.rollover_period == receiver_epoch->rollover_period)
    {
      const std::uint64_t period = *receiver_epoch->rollover_period;
      advanced = previous.value > (period * 3u) / 4u && receiver_epoch->value < period / 4u;
    }

    metrics_.consecutive_identical_receiver_epochs = 1u;
    if (advanced)
    {
      ++metrics_.receiver_epoch_advances;
      metrics_.last_receiver_epoch_relation = ReceiverEpochRelation::kAdvanced;
    }
    else
    {
      ++metrics_.receiver_epoch_regressions;
      metrics_.last_receiver_epoch_relation = ReceiverEpochRelation::kRegressed;
    }
  }

  PositionPayloadFreshnessMetrics metrics_{};
  std::optional<Position> comparable_position_{};
};

inline const char* ToString(const ReceiverEpochDomain domain)
{
  switch (domain)
  {
    case ReceiverEpochDomain::kUtcNanosecondsOfDay:
      return "utc_nanoseconds_of_day";
    case ReceiverEpochDomain::kGpsTowMilliseconds:
      return "gps_tow_milliseconds";
    case ReceiverEpochDomain::kGpsWeekTowMilliseconds:
      return "gps_week_tow_milliseconds";
    case ReceiverEpochDomain::kReceiverWeekTowMilliseconds:
      return "receiver_week_tow_milliseconds";
    case ReceiverEpochDomain::kUnknown:
    default:
      return "unknown";
  }
}

inline const char* ToString(const ReceiverEpochRelation relation)
{
  switch (relation)
  {
    case ReceiverEpochRelation::kFirst:
      return "first";
    case ReceiverEpochRelation::kAdvanced:
      return "advanced";
    case ReceiverEpochRelation::kIdentical:
      return "identical";
    case ReceiverEpochRelation::kRegressed:
      return "regressed";
    case ReceiverEpochRelation::kDomainChanged:
      return "domain_changed";
    case ReceiverEpochRelation::kUnavailable:
    default:
      return "unavailable";
  }
}

}  // namespace universal_gnss_driver
