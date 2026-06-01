#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/rtcm_records.hpp"

namespace universal_gnss_protocols
{

struct RtcmCorrectionActivityStats
{
  std::uint64_t count{0};
  std::optional<ProtocolTimestampNs> last_seen_timestamp_ns{};
};

using RtcmMessageTypeActivityMap = std::map<std::uint16_t, RtcmCorrectionActivityStats>;
using RtcmMsmConstellationActivityMap =
    std::map<RtcmConstellation, RtcmCorrectionActivityStats>;

struct RtcmCorrectionHealthOptions
{
  std::optional<ProtocolTimestampNs> now_timestamp_ns{};
  ProtocolTimestampNs stale_after_ns{0};
  std::vector<std::uint16_t> required_message_types{};
  bool require_any_msm{false};
  bool require_base_position{false};
  bool require_glonass_bias{false};
};

class RtcmCorrectionMonitor
{
public:
  void Reset();

  void ObserveFrame(const RtcmFrame& frame);
  void ObserveMessage(const RtcmMessageInfo& info,
                      std::optional<ProtocolTimestampNs> timestamp_ns = std::nullopt);
  void ObserveInvalidFrame(std::optional<ProtocolTimestampNs> timestamp_ns = std::nullopt);

  std::uint64_t total_frames() const;
  std::uint64_t valid_frames() const;
  std::uint64_t invalid_frames() const;
  std::optional<ProtocolTimestampNs> last_frame_timestamp_ns() const;

  const RtcmMessageTypeActivityMap& message_type_activity() const;
  const RtcmMsmConstellationActivityMap& msm_constellation_activity() const;

  std::uint64_t MessageCount(std::uint16_t message_type) const;
  std::optional<ProtocolTimestampNs> LastSeenMessageTimestampNs(std::uint16_t message_type) const;
  std::uint64_t MsmConstellationCount(RtcmConstellation constellation) const;
  std::optional<ProtocolTimestampNs> LastSeenMsmConstellationTimestampNs(
      RtcmConstellation constellation) const;

  bool HasSeenBasePositionMessage() const;
  bool HasBaseStationPosition() const;
  bool HasSeenBasePosition1005() const;
  bool HasSeenBasePosition1006() const;
  bool HasSeenGlonassBias1230() const;
  bool HasSeenAnyMsmMessage() const;

  const std::optional<RtcmBaseStationArpRecord>& last_base_station_arp() const;
  std::optional<ProtocolTimestampNs> LastBaseStationArpTimestampNs() const;

  bool HasRequiredMessageTypes(const std::vector<std::uint16_t>& message_types) const;
  bool HasRequiredCorrectionMessages(const RtcmCorrectionHealthOptions& options) const;

  std::optional<ProtocolTimestampNs> AgeSinceLastFrameNs(ProtocolTimestampNs now_timestamp_ns) const;
  std::optional<ProtocolTimestampNs> AgeSinceMessageTypeNs(std::uint16_t message_type,
                                                           ProtocolTimestampNs now_timestamp_ns) const;
  std::optional<ProtocolTimestampNs> AgeSinceMsmConstellationNs(
      RtcmConstellation constellation,
      ProtocolTimestampNs now_timestamp_ns) const;
  std::optional<ProtocolTimestampNs> AgeSinceBaseStationArpNs(
      ProtocolTimestampNs now_timestamp_ns) const;

  std::optional<double> TotalFrameRateHz(ProtocolTimestampNs window_end_timestamp_ns,
                                         ProtocolTimestampNs window_duration_ns) const;
  std::optional<double> ValidFrameRateHz(ProtocolTimestampNs window_end_timestamp_ns,
                                         ProtocolTimestampNs window_duration_ns) const;
  std::optional<double> MessageRateHz(std::uint16_t message_type,
                                      ProtocolTimestampNs window_end_timestamp_ns,
                                      ProtocolTimestampNs window_duration_ns) const;
  std::optional<double> MsmConstellationRateHz(RtcmConstellation constellation,
                                               ProtocolTimestampNs window_end_timestamp_ns,
                                               ProtocolTimestampNs window_duration_ns) const;

private:
  std::uint64_t total_frames_{0};
  std::uint64_t valid_frames_{0};
  std::uint64_t invalid_frames_{0};
  std::optional<ProtocolTimestampNs> last_frame_timestamp_ns_{};

  RtcmMessageTypeActivityMap message_type_activity_{};
  RtcmMsmConstellationActivityMap msm_constellation_activity_{};

  std::map<std::uint16_t, std::vector<ProtocolTimestampNs>> message_type_timestamps_{};
  std::map<RtcmConstellation, std::vector<ProtocolTimestampNs>> msm_constellation_timestamps_{};
  std::vector<ProtocolTimestampNs> total_frame_timestamps_{};
  std::vector<ProtocolTimestampNs> valid_frame_timestamps_{};

  bool seen_base_position_1005_{false};
  bool seen_base_position_1006_{false};
  bool seen_glonass_bias_1230_{false};
  std::optional<RtcmBaseStationArpRecord> last_base_station_arp_{};
  std::optional<ProtocolTimestampNs> last_base_station_arp_timestamp_ns_{};

  void RecordValidMessage(const RtcmMessageInfo& info,
                          std::optional<ProtocolTimestampNs> timestamp_ns);
};

universal_gnss::GnssHealthSummary BuildRtcmCorrectionHealth(
    const RtcmCorrectionMonitor& monitor,
    const RtcmCorrectionHealthOptions& options);

}  // namespace universal_gnss_protocols
