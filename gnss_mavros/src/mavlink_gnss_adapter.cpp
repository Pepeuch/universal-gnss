#include "universal_gnss_mavros/mavlink_gnss_adapter.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace universal_gnss_mavros {
namespace {

using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss::GnssRuntimeState;

constexpr std::uint32_t kSerialHalfRange = UINT32_C(0x80000000);

bool IsKnownPositiveAccuracy(const std::optional<std::uint32_t>& value)
{
  return value.has_value() && *value > 0u && *value != UINT32_MAX;
}

bool HasPositionFix(const std::uint8_t fix_type) { return fix_type >= 2u && fix_type <= 8u; }

GnssFixType MapFixType(const std::uint8_t fix_type)
{
  switch (fix_type)
  {
  case 0u:
  case 1u:
    return GnssFixType::kNoFix;
  case 5u:
    return GnssFixType::kRtkFloat;
  case 6u:
    return GnssFixType::kRtkFixed;
  case 2u:
  case 3u:
  case 4u:
  case 7u:
  case 8u:
    return GnssFixType::kFix;
  default:
    // An out-of-range MAVLink enum is not a usable fix. Mapping it to the
    // explicit no-fix state also prevents an older valid fix from surviving.
    return GnssFixType::kNoFix;
  }
}

std::optional<GnssRtkMode> MapRtkMode(const std::uint8_t fix_type)
{
  if (fix_type == 5u)
  {
    return GnssRtkMode::kFloat;
  }
  if (fix_type == 6u)
  {
    return GnssRtkMode::kFixed;
  }
  if (fix_type <= 8u)
  {
    return GnssRtkMode::kNone;
  }
  return std::nullopt;
}

GnssRuntimeState ToRuntimeUpdate(const RawGpsObservation& observation,
                                 const universal_gnss::GnssTimestampNs receipt_timestamp_ns)
{
  GnssRuntimeState update;
  update.timestamp_ns = receipt_timestamp_ns;
  update.fix_type = MapFixType(observation.fix_type);
  update.fix_valid = HasPositionFix(observation.fix_type);

  universal_gnss::SetCapability(update, GnssCapability::kRtkMode);
  universal_gnss::SetCapability(update, GnssCapability::kHorizontalAccuracy);
  universal_gnss::SetCapability(update, GnssCapability::kVerticalAccuracy);
  universal_gnss::SetCapability(update, GnssCapability::kHdop);
  universal_gnss::SetCapability(update, GnssCapability::kVdop);
  universal_gnss::SetCapability(update, GnssCapability::kSatellitesVisible);
  universal_gnss::SetCapability(update, GnssCapability::kSpeedOverGround);
  universal_gnss::SetCapability(update, GnssCapability::kCourseOverGround);
  universal_gnss::SetCapability(update, GnssCapability::kDifferentialCorrections);
  universal_gnss::SetCapability(update, GnssCapability::kCorrectionsActive);
  if (observation.supports_dgps_age)
  {
    universal_gnss::SetCapability(update, GnssCapability::kCorrectionAge);
  }

  if (const auto rtk_mode = MapRtkMode(observation.fix_type); rtk_mode.has_value())
  {
    universal_gnss::SetOptionalValue(update, GnssCapability::kRtkMode, update.rtk_mode, *rtk_mode);
  } else
  {
    universal_gnss::ClearOptionalValue(update, GnssCapability::kRtkMode, update.rtk_mode);
  }

  const bool differential_solution =
      observation.fix_type == 4u || observation.fix_type == 5u || observation.fix_type == 6u;
  universal_gnss::SetOptionalValue(update, GnssCapability::kDifferentialCorrections,
                                   update.differential_corrections, differential_solution);
  universal_gnss::SetOptionalValue(update, GnssCapability::kCorrectionsActive,
                                   update.corrections_active, differential_solution);

  if (update.fix_valid)
  {
    update.latitude_deg = static_cast<double>(observation.latitude_deg_e7) / 1.0e7;
    update.longitude_deg = static_cast<double>(observation.longitude_deg_e7) / 1.0e7;
    update.altitude_m = static_cast<double>(observation.altitude_msl_mm) / 1000.0;
  } else
  {
    universal_gnss::ClearPositionValues(update);
  }

  if (update.fix_valid && IsKnownPositiveAccuracy(observation.horizontal_accuracy_mm))
  {
    universal_gnss::SetOptionalValue(
        update, GnssCapability::kHorizontalAccuracy, update.horizontal_accuracy_m,
        static_cast<float>(*observation.horizontal_accuracy_mm) / 1000.0f);
  } else
  {
    universal_gnss::ClearOptionalValue(update, GnssCapability::kHorizontalAccuracy,
                                       update.horizontal_accuracy_m);
  }
  if (update.fix_valid && IsKnownPositiveAccuracy(observation.vertical_accuracy_mm))
  {
    universal_gnss::SetOptionalValue(
        update, GnssCapability::kVerticalAccuracy, update.vertical_accuracy_m,
        static_cast<float>(*observation.vertical_accuracy_mm) / 1000.0f);
  } else
  {
    universal_gnss::ClearOptionalValue(update, GnssCapability::kVerticalAccuracy,
                                       update.vertical_accuracy_m);
  }

  if (update.fix_valid && observation.hdop_centi != UINT16_MAX)
  {
    universal_gnss::SetOptionalValue(update, GnssCapability::kHdop, update.hdop,
                                     observation.hdop_centi / 100.0f);
  } else
  {
    universal_gnss::ClearOptionalValue(update, GnssCapability::kHdop, update.hdop);
  }
  if (update.fix_valid && observation.vdop_centi != UINT16_MAX)
  {
    universal_gnss::SetOptionalValue(update, GnssCapability::kVdop, update.vdop,
                                     observation.vdop_centi / 100.0f);
  } else
  {
    universal_gnss::ClearOptionalValue(update, GnssCapability::kVdop, update.vdop);
  }

  if (observation.satellites_visible != UINT8_MAX)
  {
    universal_gnss::SetOptionalValue(update, GnssCapability::kSatellitesVisible,
                                     update.satellites_visible,
                                     static_cast<std::uint16_t>(observation.satellites_visible));
  } else
  {
    universal_gnss::ClearOptionalValue(update, GnssCapability::kSatellitesVisible,
                                       update.satellites_visible);
  }

  if (update.fix_valid && observation.ground_speed_cm_s != UINT16_MAX)
  {
    universal_gnss::SetOptionalValue(update, GnssCapability::kSpeedOverGround,
                                     update.speed_over_ground_m_s,
                                     observation.ground_speed_cm_s / 100.0f);
  } else
  {
    universal_gnss::ClearOptionalValue(update, GnssCapability::kSpeedOverGround,
                                       update.speed_over_ground_m_s);
  }
  if (update.fix_valid && observation.course_cdeg != UINT16_MAX && observation.course_cdeg < 36000u)
  {
    universal_gnss::SetOptionalValue(update, GnssCapability::kCourseOverGround,
                                     update.course_over_ground_deg,
                                     observation.course_cdeg / 100.0f);
  } else
  {
    universal_gnss::ClearOptionalValue(update, GnssCapability::kCourseOverGround,
                                       update.course_over_ground_deg);
  }

  if (observation.supports_dgps_age)
  {
    if (observation.dgps_age_ms.has_value() && *observation.dgps_age_ms != UINT32_MAX)
    {
      universal_gnss::SetOptionalValue(update, GnssCapability::kCorrectionAge,
                                       update.correction_age_s,
                                       static_cast<float>(*observation.dgps_age_ms) / 1000.0f);
    } else
    {
      universal_gnss::ClearOptionalValue(update, GnssCapability::kCorrectionAge,
                                         update.correction_age_s);
    }
  }

  return update;
}

} // namespace

MavlinkGnssAdapter::MavlinkGnssAdapter(AdapterConfig config) : config_(std::move(config))
{
  if (config_.source_id_prefix.empty() || config_.source_incarnation_prefix.empty())
  {
    throw std::invalid_argument("MAVLink GNSS source and incarnation prefixes must be non-empty");
  }
}

bool MavlinkGnssAdapter::OnConnectionChanged(const bool connected)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (connected_ == connected)
  {
    return false;
  }

  connected_ = connected;
  StartNewIncarnationLocked();
  return true;
}

bool MavlinkGnssAdapter::HandleSystemTime(const SystemTimeObservation& observation)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const bool regressed =
      system_time_metadata_.has_value() &&
      IsWrapAwareRegression(system_time_metadata_->boot_time_ms, observation.boot_time_ms);
  if (regressed)
  {
    StartNewIncarnationLocked();
  }
  system_time_metadata_ = observation;
  return regressed;
}

void MavlinkGnssAdapter::HandleRawGps(const Receiver receiver, const RawGpsObservation& observation,
                                      const universal_gnss::GnssTimestampNs receipt_timestamp_ns)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto& state = receivers_.at(Index(receiver));
  state.aggregator.Merge(ToRuntimeUpdate(observation, receipt_timestamp_ns));
  state.raw_metadata = observation;
  ++state.position_observation_sequence;
}

void MavlinkGnssAdapter::HandleRtk(const Receiver receiver, const RtkObservation& observation,
                                   const universal_gnss::GnssTimestampNs receipt_timestamp_ns)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto& receiver_state = receivers_.at(Index(receiver));
  GnssRuntimeState update;
  update.timestamp_ns = receipt_timestamp_ns;
  universal_gnss::SetCapability(update, GnssCapability::kSatellitesUsed);
  universal_gnss::SetOptionalValue(update, GnssCapability::kSatellitesUsed, update.satellites_used,
                                   static_cast<std::uint16_t>(observation.satellites_used));
  receiver_state.aggregator.Merge(update);
  receiver_state.rtk_metadata = observation;
}

ReceiverSnapshot MavlinkGnssAdapter::Snapshot(const Receiver receiver) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto& receiver_state = receivers_.at(Index(receiver));
  ReceiverSnapshot snapshot;
  snapshot.state = receiver_state.aggregator.state();
  snapshot.source_id = SourceIdLocked(receiver);
  snapshot.source_incarnation = SourceIncarnationLocked();
  snapshot.position_observation_sequence = receiver_state.position_observation_sequence;
  snapshot.incarnation_generation = incarnation_generation_;
  snapshot.fcu_connected = connected_;
  snapshot.raw_metadata = receiver_state.raw_metadata;
  snapshot.rtk_metadata = receiver_state.rtk_metadata;
  snapshot.system_time_metadata = system_time_metadata_;
  return snapshot;
}

std::size_t MavlinkGnssAdapter::Index(const Receiver receiver)
{
  switch (receiver)
  {
  case Receiver::kGps1:
    return 0u;
  case Receiver::kGps2:
    return 1u;
  }
  throw std::invalid_argument("unknown MAVLink GNSS receiver");
}

bool MavlinkGnssAdapter::IsWrapAwareRegression(const std::uint32_t previous,
                                               const std::uint32_t current)
{
  if (current == previous)
  {
    return false;
  }
  const std::uint32_t forward_delta = current - previous;
  // Exactly half the counter range is ambiguous, so do not claim reboot
  // evidence there. Values beyond the half-range are unambiguously backwards.
  return forward_delta > kSerialHalfRange;
}

void MavlinkGnssAdapter::StartNewIncarnationLocked()
{
  ++incarnation_generation_;
  for (auto& receiver : receivers_)
  {
    receiver.aggregator.Reset();
    receiver.position_observation_sequence = 0u;
    receiver.raw_metadata.reset();
    receiver.rtk_metadata.reset();
  }
  system_time_metadata_.reset();
}

std::string MavlinkGnssAdapter::SourceIdLocked(const Receiver receiver) const
{
  return config_.source_id_prefix + (receiver == Receiver::kGps1 ? ":gps1" : ":gps2");
}

std::string MavlinkGnssAdapter::SourceIncarnationLocked() const
{
  return config_.source_incarnation_prefix + ":" + std::to_string(incarnation_generation_);
}

} // namespace universal_gnss_mavros
