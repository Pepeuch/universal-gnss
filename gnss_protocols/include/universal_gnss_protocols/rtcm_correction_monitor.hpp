#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
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

struct RtcmMsmSummaryActivityStats
{
  std::uint64_t decode_success_count{0};
  std::uint64_t decode_failure_count{0};
  std::uint64_t malformed_count{0};
  std::optional<ProtocolTimestampNs> last_decoded_timestamp_ns{};
  std::optional<RtcmMsmSummaryRecord> last_summary{};
};

using RtcmMsmSummaryActivityMap = std::map<std::uint16_t, RtcmMsmSummaryActivityStats>;

struct RtcmCorrectionHealthOptions
{
  std::optional<ProtocolTimestampNs> now_timestamp_ns{};
  ProtocolTimestampNs stale_after_ns{0};
  std::vector<std::uint16_t> required_message_types{};
  std::vector<RtcmConstellation> required_msm_constellations{};
  ProtocolTimestampNs required_observation_window_ns{0};
  ProtocolTimestampNs startup_grace_ns{0};
  bool require_any_msm{false};
  bool require_base_position{false};
  bool require_glonass_bias{false};
};

struct RtcmSemanticField
{
  std::string key{};
  std::string value{};
};

struct RtcmSemanticObservation
{
  std::string name{};
  std::uint16_t message_type{0};
  bool seen{false};
  bool decoded{false};
  bool valid{false};
  std::uint64_t decode_success_count{0};
  std::uint64_t decode_failure_count{0};
  std::uint64_t malformed_count{0};
  std::optional<ProtocolTimestampNs> last_seen_timestamp_ns{};
  std::optional<ProtocolTimestampNs> last_decoded_timestamp_ns{};
  std::optional<ProtocolTimestampNs> age_ns{};
  std::vector<RtcmSemanticField> fields{};
};

using RtcmSemanticObservations = std::vector<RtcmSemanticObservation>;

void ConfigurePortableRtkCorrectionRequirements(RtcmCorrectionHealthOptions& options);

class RtcmCorrectionMonitor
{
public:
  // Rate queries retain the current diagnostic horizon plus a one-window margin.
  static constexpr ProtocolTimestampNs kTimestampHistoryRetentionNs = 60000000000LL;

  void Reset();
  // Preserve decoded, station-owned 1005/1006 metadata while clearing
  // session/dynamic correction observations (MSM, 1230, rates, freshness).
  void ResetDynamicState();

  void ObserveFrame(const RtcmFrame& frame);
  void ObserveMessage(const RtcmMessageInfo& info,
                      std::optional<ProtocolTimestampNs> timestamp_ns = std::nullopt);
  void ObserveInvalidFrame(std::optional<ProtocolTimestampNs> timestamp_ns = std::nullopt);

  std::uint64_t total_frames() const;
  std::uint64_t valid_frames() const;
  std::uint64_t invalid_frames() const;
  std::size_t RetainedTimestampCount() const;
  std::optional<ProtocolTimestampNs> last_frame_timestamp_ns() const;
  std::optional<ProtocolTimestampNs> first_valid_frame_timestamp_ns() const;
  std::optional<std::uint16_t> station_id() const;

  const RtcmMessageTypeActivityMap& message_type_activity() const;
  const RtcmMsmConstellationActivityMap& msm_constellation_activity() const;
  const RtcmMsmSummaryActivityMap& msm_summary_activity() const;

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
  bool HasDecodedGlonassBias1230() const;
  bool LastGlonassBias1230Valid() const;
  bool HasSeenAnyMsmMessage() const;
  bool HasDecodedAnyMsmSummary() const;

  const std::optional<RtcmBaseStationArpRecord>& last_base_station_arp() const;
  std::optional<ProtocolTimestampNs> LastBaseStationArpTimestampNs() const;
  std::uint64_t BaseStationArpDecodeSuccessCount() const;
  std::uint64_t BaseStationArpDecodeFailureCount() const;
  std::uint64_t BaseStationArpMalformedCount() const;
  const std::optional<RtcmGlonassCodePhaseBiasRecord>& last_glonass_code_phase_bias() const;
  std::optional<ProtocolTimestampNs> LastGlonassBias1230TimestampNs() const;
  std::optional<ProtocolTimestampNs> LastDecodedGlonassBias1230TimestampNs() const;
  std::uint64_t GlonassBias1230DecodeSuccessCount() const;
  std::uint64_t GlonassBias1230DecodeFailureCount() const;
  std::uint64_t GlonassBias1230MalformedCount() const;
  const std::optional<RtcmMsmSummaryRecord>& last_msm_summary() const;
  std::optional<ProtocolTimestampNs> LastMsmTimestampNs() const;
  std::optional<ProtocolTimestampNs> LastDecodedMsmTimestampNs() const;
  std::uint64_t MsmDecodeSuccessCount() const;
  std::uint64_t MsmDecodeFailureCount() const;
  std::uint64_t MsmMalformedCount() const;

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
  std::optional<ProtocolTimestampNs> AgeSinceGlonassBias1230Ns(
      ProtocolTimestampNs now_timestamp_ns) const;
  std::optional<ProtocolTimestampNs> AgeSinceLastMsmNs(ProtocolTimestampNs now_timestamp_ns) const;

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
  std::optional<ProtocolTimestampNs> first_valid_frame_timestamp_ns_{};

  RtcmMessageTypeActivityMap message_type_activity_{};
  RtcmMsmConstellationActivityMap msm_constellation_activity_{};
  RtcmMsmSummaryActivityMap msm_summary_activity_{};
  // Frame activity supports integrity/flow diagnostics. Semantic activity is
  // populated only after the message-specific decoder accepts the payload and
  // is the sole source for correction requirement satisfaction.
  RtcmMessageTypeActivityMap semantic_message_type_activity_{};
  RtcmMsmConstellationActivityMap semantic_msm_constellation_activity_{};

  std::map<std::uint16_t, std::multiset<ProtocolTimestampNs>> message_type_timestamps_{};
  std::map<RtcmConstellation, std::multiset<ProtocolTimestampNs>> msm_constellation_timestamps_{};
  std::multiset<ProtocolTimestampNs> total_frame_timestamps_{};
  std::multiset<ProtocolTimestampNs> valid_frame_timestamps_{};

  bool seen_base_position_1005_{false};
  bool seen_base_position_1006_{false};
  bool seen_glonass_bias_1230_{false};
  std::optional<RtcmBaseStationArpRecord> last_base_station_arp_{};
  std::optional<ProtocolTimestampNs> last_base_station_arp_timestamp_ns_{};
  std::uint64_t base_station_arp_decode_success_count_{0};
  std::uint64_t base_station_arp_decode_failure_count_{0};
  std::uint64_t base_station_arp_malformed_count_{0};
  std::optional<RtcmGlonassCodePhaseBiasRecord> last_glonass_code_phase_bias_{};
  std::optional<ProtocolTimestampNs> last_glonass_bias_1230_timestamp_ns_{};
  std::optional<ProtocolTimestampNs> last_decoded_glonass_bias_1230_timestamp_ns_{};
  std::uint64_t glonass_bias_1230_decode_success_count_{0};
  std::uint64_t glonass_bias_1230_decode_failure_count_{0};
  std::uint64_t glonass_bias_1230_malformed_count_{0};
  std::optional<RtcmMsmSummaryRecord> last_msm_summary_{};
  std::optional<ProtocolTimestampNs> last_msm_timestamp_ns_{};
  std::optional<ProtocolTimestampNs> last_decoded_msm_timestamp_ns_{};
  std::uint64_t msm_decode_success_count_{0};
  std::uint64_t msm_decode_failure_count_{0};
  std::uint64_t msm_malformed_count_{0};
  std::optional<std::uint16_t> station_id_{};

  void PrepareStationOwnership(std::uint16_t station_id);
  void RecordValidFrameMessage(const RtcmMessageInfo& info,
                               std::optional<ProtocolTimestampNs> timestamp_ns);
  void RecordSemanticMessage(const RtcmMessageInfo& info,
                             std::optional<ProtocolTimestampNs> timestamp_ns);
};

universal_gnss::GnssHealthSummary BuildRtcmCorrectionHealth(
    const RtcmCorrectionMonitor& monitor,
    const RtcmCorrectionHealthOptions& options);

RtcmSemanticObservations BuildRtcmSemanticObservations(
    const RtcmCorrectionMonitor& monitor,
    std::optional<ProtocolTimestampNs> now_timestamp_ns = std::nullopt);

}  // namespace universal_gnss_protocols
