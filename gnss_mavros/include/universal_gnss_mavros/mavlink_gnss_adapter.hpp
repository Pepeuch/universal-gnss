#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "universal_gnss/gnss_runtime_aggregator.hpp"

namespace universal_gnss_mavros {

enum class Receiver : std::uint8_t
{
  kGps1 = 0,
  kGps2 = 1,
};

struct RawGpsObservation
{
  std::uint64_t mavlink_time_usec{0u};
  std::uint8_t fix_type{0u};
  std::int32_t latitude_deg_e7{0};
  std::int32_t longitude_deg_e7{0};
  std::int32_t altitude_msl_mm{0};
  std::uint16_t hdop_centi{UINT16_MAX};
  std::uint16_t vdop_centi{UINT16_MAX};
  std::uint16_t ground_speed_cm_s{UINT16_MAX};
  std::uint16_t course_cdeg{UINT16_MAX};
  std::uint8_t satellites_visible{UINT8_MAX};
  std::optional<std::uint32_t> horizontal_accuracy_mm{};
  std::optional<std::uint32_t> vertical_accuracy_mm{};
  bool supports_dgps_age{false};
  std::optional<std::uint32_t> dgps_age_ms{};
};

struct RtkObservation
{
  std::uint32_t time_last_baseline_ms{0u};
  std::uint8_t rtk_receiver_id{0u};
  std::int16_t gps_week{0};
  std::uint32_t gps_time_of_week_ms{0u};
  std::uint8_t health{0u};
  std::uint8_t rate_hz{0u};
  std::uint8_t satellites_used{0u};
  std::uint8_t baseline_coordinate_system{0u};
  std::int32_t baseline_a_mm{0};
  std::int32_t baseline_b_mm{0};
  std::int32_t baseline_c_mm{0};
  std::uint32_t accuracy{0u};
  std::int32_t iar_num_hypotheses{0};
};

struct SystemTimeObservation
{
  std::uint64_t unix_time_usec{0u};
  std::uint32_t boot_time_ms{0u};
};

struct ReceiverSnapshot
{
  universal_gnss::GnssRuntimeState state{};
  std::string source_id{};
  std::string source_incarnation{};
  std::uint64_t position_observation_sequence{0u};
  std::uint64_t incarnation_generation{0u};
  bool fcu_connected{false};
  std::optional<RawGpsObservation> raw_metadata{};
  std::optional<RtkObservation> rtk_metadata{};
  std::optional<SystemTimeObservation> system_time_metadata{};
};

struct AdapterConfig
{
  std::string source_id_prefix{"mavlink:fcu"};
  std::string source_incarnation_prefix{"mavros"};
};

class MavlinkGnssAdapter
{
public:
  explicit MavlinkGnssAdapter(AdapterConfig config = {});

  bool OnConnectionChanged(bool connected);
  bool HandleSystemTime(const SystemTimeObservation& observation);
  void HandleRawGps(Receiver receiver, const RawGpsObservation& observation,
                    universal_gnss::GnssTimestampNs receipt_timestamp_ns);
  void HandleRtk(Receiver receiver, const RtkObservation& observation,
                 universal_gnss::GnssTimestampNs receipt_timestamp_ns);

  ReceiverSnapshot Snapshot(Receiver receiver) const;

private:
  struct ReceiverState
  {
    universal_gnss::GnssRuntimeAggregator aggregator{};
    std::uint64_t position_observation_sequence{0u};
    std::optional<RawGpsObservation> raw_metadata{};
    std::optional<RtkObservation> rtk_metadata{};
  };

  static std::size_t Index(Receiver receiver);
  static bool IsWrapAwareRegression(std::uint32_t previous, std::uint32_t current);
  void StartNewIncarnationLocked();
  std::string SourceIdLocked(Receiver receiver) const;
  std::string SourceIncarnationLocked() const;

  mutable std::mutex mutex_{};
  AdapterConfig config_{};
  std::array<ReceiverState, 2> receivers_{};
  bool connected_{false};
  std::uint64_t incarnation_generation_{1u};
  std::optional<SystemTimeObservation> system_time_metadata_{};
};

} // namespace universal_gnss_mavros
