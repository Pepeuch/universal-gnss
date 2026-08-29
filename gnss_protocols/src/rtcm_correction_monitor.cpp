#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"

#include <cstddef>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>

#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"

namespace universal_gnss_protocols
{

namespace
{

void UpdateLatestTimestamp(const std::optional<ProtocolTimestampNs>& candidate,
                           std::optional<ProtocolTimestampNs>& current)
{
  if (!candidate.has_value())
  {
    return;
  }

  if (!current.has_value() || *candidate >= *current)
  {
    current = candidate;
  }
}

void AppendTimestamp(const std::optional<ProtocolTimestampNs>& timestamp_ns,
                     std::multiset<ProtocolTimestampNs>& timestamps)
{
  if (!timestamp_ns.has_value())
  {
    return;
  }

  timestamps.insert(*timestamp_ns);
  const ProtocolTimestampNs latest_timestamp_ns = *timestamps.rbegin();
  if (latest_timestamp_ns <=
      std::numeric_limits<ProtocolTimestampNs>::min() +
          RtcmCorrectionMonitor::kTimestampHistoryRetentionNs)
  {
    return;
  }

  const ProtocolTimestampNs retention_start_timestamp_ns =
      latest_timestamp_ns - RtcmCorrectionMonitor::kTimestampHistoryRetentionNs;
  timestamps.erase(timestamps.begin(), timestamps.lower_bound(retention_start_timestamp_ns));
}

std::size_t CountTimestampsInWindow(const std::multiset<ProtocolTimestampNs>& timestamps,
                                    const ProtocolTimestampNs window_end_timestamp_ns,
                                    const ProtocolTimestampNs window_duration_ns)
{
  const ProtocolTimestampNs window_start_timestamp_ns =
      window_end_timestamp_ns - window_duration_ns;

  const auto begin = timestamps.lower_bound(window_start_timestamp_ns);
  const auto end = timestamps.upper_bound(window_end_timestamp_ns);
  return static_cast<std::size_t>(std::distance(begin, end));
}

std::optional<double> ComputeRateHz(const std::multiset<ProtocolTimestampNs>& timestamps,
                                    const ProtocolTimestampNs window_end_timestamp_ns,
                                    const ProtocolTimestampNs window_duration_ns)
{
  if (window_duration_ns <= 0 ||
      window_duration_ns > RtcmCorrectionMonitor::kTimestampHistoryRetentionNs)
  {
    return std::nullopt;
  }

  if (timestamps.empty())
  {
    return std::nullopt;
  }

  const double window_duration_s =
      static_cast<double>(window_duration_ns) / 1000000000.0;
  const std::size_t count_in_window =
      CountTimestampsInWindow(timestamps, window_end_timestamp_ns, window_duration_ns);
  return static_cast<double>(count_in_window) / window_duration_s;
}

std::optional<ProtocolTimestampNs> ComputeAgeSince(
    const std::optional<ProtocolTimestampNs>& last_seen_timestamp_ns,
    const ProtocolTimestampNs now_timestamp_ns)
{
  if (!last_seen_timestamp_ns.has_value())
  {
    return std::nullopt;
  }

  if (*last_seen_timestamp_ns > now_timestamp_ns)
  {
    return std::nullopt;
  }

  return now_timestamp_ns - *last_seen_timestamp_ns;
}

bool HasActivityInWindow(const std::uint64_t observation_count,
                         const std::optional<ProtocolTimestampNs>& last_seen_timestamp_ns,
                         const std::optional<ProtocolTimestampNs>& window_start_timestamp_ns,
                         const std::optional<ProtocolTimestampNs>& window_end_timestamp_ns)
{
  if (observation_count == 0u)
  {
    return false;
  }

  if (window_end_timestamp_ns.has_value() && last_seen_timestamp_ns.has_value() &&
      *last_seen_timestamp_ns > *window_end_timestamp_ns)
  {
    return false;
  }

  if (!window_start_timestamp_ns.has_value())
  {
    return true;
  }

  return last_seen_timestamp_ns.has_value() &&
         *last_seen_timestamp_ns >= *window_start_timestamp_ns;
}

std::optional<ProtocolTimestampNs> ComputeObservationWindowStart(
    const RtcmCorrectionHealthOptions& options)
{
  if (!options.now_timestamp_ns.has_value() || options.required_observation_window_ns <= 0)
  {
    return std::nullopt;
  }

  return *options.now_timestamp_ns - options.required_observation_window_ns;
}

bool IsWithinStartupGrace(const RtcmCorrectionMonitor& monitor,
                          const RtcmCorrectionHealthOptions& options)
{
  if (options.startup_grace_ns <= 0 || !options.now_timestamp_ns.has_value())
  {
    return false;
  }

  const auto first_valid_timestamp_ns = monitor.first_valid_frame_timestamp_ns();
  if (!first_valid_timestamp_ns.has_value())
  {
    return false;
  }

  if (*first_valid_timestamp_ns > *options.now_timestamp_ns)
  {
    return false;
  }

  return *options.now_timestamp_ns - *first_valid_timestamp_ns < options.startup_grace_ns;
}

std::string FormatDoubleValue(const double value, const int precision = 4)
{
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(precision) << value;
  return stream.str();
}

std::string FormatMaskValue(const std::uint8_t value)
{
  std::ostringstream stream;
  stream << "0x" << std::hex << std::uppercase << static_cast<unsigned int>(value);
  return stream.str();
}

std::string DescribeRtcmConstellation(const RtcmConstellation constellation)
{
  switch (constellation)
  {
    case RtcmConstellation::kGps:
      return "gps";
    case RtcmConstellation::kGlonass:
      return "glonass";
    case RtcmConstellation::kGalileo:
      return "galileo";
    case RtcmConstellation::kSbas:
      return "sbas";
    case RtcmConstellation::kQzss:
      return "qzss";
    case RtcmConstellation::kBeiDou:
      return "beidou";
    case RtcmConstellation::kNavIc:
      return "navic";
    case RtcmConstellation::kUnknown:
    default:
      return "unknown";
  }
}

std::string JoinStrings(const std::vector<std::string>& values)
{
  std::ostringstream stream;
  for (std::size_t index = 0u; index < values.size(); ++index)
  {
    if (index != 0u)
    {
      stream << ',';
    }
    stream << values[index];
  }
  return stream.str();
}

std::string BuildMsmObservationName(const RtcmConstellation constellation,
                                    const std::uint8_t msm_variant)
{
  std::ostringstream stream;
  stream << "msm_" << DescribeRtcmConstellation(constellation);
  if (msm_variant != 0u)
  {
    stream << "_msm" << static_cast<unsigned int>(msm_variant);
  }
  return stream.str();
}

std::uint16_t FindLatestSeenMsmMessageType(const RtcmCorrectionMonitor& monitor)
{
  std::optional<ProtocolTimestampNs> latest_timestamp_ns{};
  std::uint16_t latest_message_type = 0u;

  for (const auto& entry : monitor.message_type_activity())
  {
    if (!IsRtcmMsmMessage(entry.first))
    {
      continue;
    }

    if (!entry.second.last_seen_timestamp_ns.has_value())
    {
      if (latest_message_type == 0u && entry.second.count > 0u)
      {
        latest_message_type = entry.first;
      }
      continue;
    }

    if (!latest_timestamp_ns.has_value() ||
        *entry.second.last_seen_timestamp_ns >= *latest_timestamp_ns)
    {
      latest_timestamp_ns = entry.second.last_seen_timestamp_ns;
      latest_message_type = entry.first;
    }
  }

  return latest_message_type;
}

}  // namespace

void ConfigurePortableRtkCorrectionRequirements(RtcmCorrectionHealthOptions& options)
{
  options.required_msm_constellations.clear();
  options.require_any_msm = true;
  options.require_base_position = true;
  // RTCM 1230 (GLONASS L1/L2 code-phase bias) is NOT required for RTK. It only
  // reconciles GLONASS inter-frequency code biases between mixed-vendor base and
  // rover hardware; modern receivers reach RTK Fixed without it. Many public
  // casters (e.g. the Centipede/crtk.net network) either omit 1230 entirely or
  // transmit it with the code-phase-bias validity indicator cleared. Requiring a
  // valid 1230 therefore produced a persistent, spurious ERROR-level
  // `rtcm.required_messages_missing` even while MSM observations and the base
  // position were streaming and RTK was working. Treat 1230 as optional so
  // correction availability tracks the messages that actually drive RTK.
  options.require_glonass_bias = false;
}

void RtcmCorrectionMonitor::Reset()
{
  total_frames_ = 0;
  valid_frames_ = 0;
  invalid_frames_ = 0;
  last_frame_timestamp_ns_.reset();
  first_valid_frame_timestamp_ns_.reset();
  message_type_activity_.clear();
  msm_constellation_activity_.clear();
  msm_summary_activity_.clear();
  semantic_message_type_activity_.clear();
  semantic_msm_constellation_activity_.clear();
  message_type_timestamps_.clear();
  msm_constellation_timestamps_.clear();
  total_frame_timestamps_.clear();
  valid_frame_timestamps_.clear();
  seen_base_position_1005_ = false;
  seen_base_position_1006_ = false;
  seen_glonass_bias_1230_ = false;
  last_base_station_arp_.reset();
  last_base_station_arp_timestamp_ns_.reset();
  base_station_arp_decode_success_count_ = 0;
  base_station_arp_decode_failure_count_ = 0;
  base_station_arp_malformed_count_ = 0;
  last_glonass_code_phase_bias_.reset();
  last_glonass_bias_1230_timestamp_ns_.reset();
  last_decoded_glonass_bias_1230_timestamp_ns_.reset();
  glonass_bias_1230_decode_success_count_ = 0;
  glonass_bias_1230_decode_failure_count_ = 0;
  glonass_bias_1230_malformed_count_ = 0;
  last_msm_summary_.reset();
  last_msm_timestamp_ns_.reset();
  last_decoded_msm_timestamp_ns_.reset();
  msm_decode_success_count_ = 0;
  msm_decode_failure_count_ = 0;
  msm_malformed_count_ = 0;
  station_id_.reset();
}

void RtcmCorrectionMonitor::ResetDynamicState()
{
  // Static base metadata is reusable only when it was fully decoded and is
  // therefore owned by a known station. Merely seeing a 1005/1006 message type
  // is not enough to carry it into another transport session.
  if (!station_id_.has_value() || !last_base_station_arp_.has_value())
  {
    Reset();
    return;
  }

  const std::optional<std::uint16_t> retained_station_id = station_id_;
  const bool retained_1005 = seen_base_position_1005_;
  const bool retained_1006 = seen_base_position_1006_;
  const auto retained_arp = last_base_station_arp_;
  const auto retained_arp_timestamp_ns = last_base_station_arp_timestamp_ns_;
  const std::uint64_t retained_decode_success_count =
      base_station_arp_decode_success_count_;

  const auto activity_1005 = message_type_activity_.find(1005u);
  const std::optional<RtcmCorrectionActivityStats> retained_activity_1005 =
      activity_1005 == message_type_activity_.end()
          ? std::nullopt
          : std::optional<RtcmCorrectionActivityStats>{activity_1005->second};
  const auto activity_1006 = message_type_activity_.find(1006u);
  const std::optional<RtcmCorrectionActivityStats> retained_activity_1006 =
      activity_1006 == message_type_activity_.end()
          ? std::nullopt
          : std::optional<RtcmCorrectionActivityStats>{activity_1006->second};
  const auto semantic_activity_1005 = semantic_message_type_activity_.find(1005u);
  const std::optional<RtcmCorrectionActivityStats> retained_semantic_activity_1005 =
      semantic_activity_1005 == semantic_message_type_activity_.end()
          ? std::nullopt
          : std::optional<RtcmCorrectionActivityStats>{semantic_activity_1005->second};
  const auto semantic_activity_1006 = semantic_message_type_activity_.find(1006u);
  const std::optional<RtcmCorrectionActivityStats> retained_semantic_activity_1006 =
      semantic_activity_1006 == semantic_message_type_activity_.end()
          ? std::nullopt
          : std::optional<RtcmCorrectionActivityStats>{semantic_activity_1006->second};
  Reset();

  station_id_ = retained_station_id;
  seen_base_position_1005_ = retained_1005;
  seen_base_position_1006_ = retained_1006;
  last_base_station_arp_ = retained_arp;
  last_base_station_arp_timestamp_ns_ = retained_arp_timestamp_ns;
  base_station_arp_decode_success_count_ = retained_decode_success_count;
  if (retained_activity_1005.has_value())
  {
    message_type_activity_.emplace(1005u, *retained_activity_1005);
  }
  if (retained_activity_1006.has_value())
  {
    message_type_activity_.emplace(1006u, *retained_activity_1006);
  }
  if (retained_semantic_activity_1005.has_value())
  {
    semantic_message_type_activity_.emplace(1005u, *retained_semantic_activity_1005);
  }
  if (retained_semantic_activity_1006.has_value())
  {
    semantic_message_type_activity_.emplace(1006u, *retained_semantic_activity_1006);
  }
}

void RtcmCorrectionMonitor::ObserveFrame(const RtcmFrame& frame)
{
  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    ++total_frames_;
    UpdateLatestTimestamp(frame.timestamp_ns, last_frame_timestamp_ns_);
    AppendTimestamp(frame.timestamp_ns, total_frame_timestamps_);
    ++invalid_frames_;
    return;
  }

  const auto parsed_info = ParseRtcmMessageInfo(frame);
  if (parsed_info.status != ParserStatus::kRecordReady || !parsed_info.record.has_value())
  {
    ++total_frames_;
    UpdateLatestTimestamp(frame.timestamp_ns, last_frame_timestamp_ns_);
    AppendTimestamp(frame.timestamp_ns, total_frame_timestamps_);
    ++invalid_frames_;
    return;
  }

  std::optional<RtcmBaseStationArpRecord> parsed_arp_record{};
  std::optional<RtcmGlonassCodePhaseBiasRecord> parsed_bias_record{};
  std::optional<RtcmMsmSummaryRecord> parsed_msm_record{};

  if (parsed_info.record->is_station_arp)
  {
    const auto parsed_arp = ParseRtcmBaseStationArp(frame);
    if (parsed_arp.status == ParserStatus::kRecordReady && parsed_arp.record.has_value())
    {
      parsed_arp_record = *parsed_arp.record;
    }
  }

  if (parsed_info.record->is_glonass_bias)
  {
    const auto parsed_bias = ParseRtcmGlonassCodePhaseBias(frame);
    if (parsed_bias.status == ParserStatus::kRecordReady && parsed_bias.record.has_value())
    {
      parsed_bias_record = *parsed_bias.record;
    }
  }

  if (parsed_info.record->is_msm)
  {
    const auto parsed_msm = ParseRtcmMsmSummary(frame);
    if (parsed_msm.status == ParserStatus::kRecordReady && parsed_msm.record.has_value())
    {
      parsed_msm_record = *parsed_msm.record;
    }
  }

  const bool msm_observation_accepted =
      parsed_msm_record.has_value() && parsed_msm_record->cell_count > 0u;

  bool semantic_message_accepted = false;
  if (parsed_info.record->is_station_arp)
  {
    semantic_message_accepted = parsed_arp_record.has_value();
  }
  else if (parsed_info.record->is_glonass_bias)
  {
    semantic_message_accepted = parsed_bias_record.has_value() && parsed_bias_record->valid;
  }
  else if (parsed_info.record->is_msm)
  {
    semantic_message_accepted = msm_observation_accepted;
  }

  std::optional<std::uint16_t> decoded_station_id{};
  if (parsed_arp_record.has_value())
  {
    decoded_station_id = parsed_arp_record->station_id;
  }
  else if (parsed_bias_record.has_value() && parsed_bias_record->valid)
  {
    decoded_station_id = parsed_bias_record->station_id;
  }
  else if (msm_observation_accepted)
  {
    decoded_station_id = parsed_msm_record->station_id;
  }
  if (decoded_station_id.has_value())
  {
    // Resolve ownership before recording the current frame. A station change
    // can therefore never create a transient healthy state from mixed data.
    PrepareStationOwnership(*decoded_station_id);
  }

  ++total_frames_;
  UpdateLatestTimestamp(frame.timestamp_ns, last_frame_timestamp_ns_);
  AppendTimestamp(frame.timestamp_ns, total_frame_timestamps_);

  if (parsed_info.record->is_station_arp)
  {
    if (parsed_arp_record.has_value())
    {
      last_base_station_arp_ = *parsed_arp_record;
      ++base_station_arp_decode_success_count_;
      UpdateLatestTimestamp(frame.timestamp_ns, last_base_station_arp_timestamp_ns_);
    }
    else
    {
      ++base_station_arp_decode_failure_count_;
      ++base_station_arp_malformed_count_;
    }
  }

  if (parsed_info.record->is_glonass_bias)
  {
    if (parsed_bias_record.has_value())
    {
      ++glonass_bias_1230_decode_success_count_;
      if (!station_id_.has_value() || *station_id_ == parsed_bias_record->station_id)
      {
        last_glonass_code_phase_bias_ = *parsed_bias_record;
        UpdateLatestTimestamp(frame.timestamp_ns, last_decoded_glonass_bias_1230_timestamp_ns_);
      }
    }
    else
    {
      ++glonass_bias_1230_decode_failure_count_;
      ++glonass_bias_1230_malformed_count_;
    }
  }

  if (parsed_info.record->is_msm)
  {
    RtcmMsmSummaryActivityStats& msm_stats = msm_summary_activity_[parsed_info.record->message_type];
    if (parsed_msm_record.has_value())
    {
      ++msm_stats.decode_success_count;
      ++msm_decode_success_count_;
      if (!station_id_.has_value() || *station_id_ == parsed_msm_record->station_id)
      {
        msm_stats.last_summary = *parsed_msm_record;
        UpdateLatestTimestamp(frame.timestamp_ns, msm_stats.last_decoded_timestamp_ns);
        last_msm_summary_ = *parsed_msm_record;
        UpdateLatestTimestamp(frame.timestamp_ns, last_decoded_msm_timestamp_ns_);
      }
    }
    else
    {
      ++msm_stats.decode_failure_count;
      ++msm_stats.malformed_count;
      ++msm_decode_failure_count_;
      ++msm_malformed_count_;
    }
  }

  RecordValidFrameMessage(*parsed_info.record, frame.timestamp_ns);
  if (semantic_message_accepted)
  {
    RecordSemanticMessage(*parsed_info.record, frame.timestamp_ns);
  }
}

void RtcmCorrectionMonitor::ObserveMessage(const RtcmMessageInfo& info,
                                           std::optional<ProtocolTimestampNs> timestamp_ns)
{
  ++total_frames_;
  UpdateLatestTimestamp(timestamp_ns, last_frame_timestamp_ns_);
  AppendTimestamp(timestamp_ns, total_frame_timestamps_);
  // ObserveMessage is the trusted, already-classified observation API. Raw
  // frames must use ObserveFrame so known semantic message types are decoded
  // before they can reach the semantic-health registry.
  RecordValidFrameMessage(info, timestamp_ns);
  RecordSemanticMessage(info, timestamp_ns);
}

void RtcmCorrectionMonitor::ObserveInvalidFrame(std::optional<ProtocolTimestampNs> timestamp_ns)
{
  ++total_frames_;
  ++invalid_frames_;
  UpdateLatestTimestamp(timestamp_ns, last_frame_timestamp_ns_);
  AppendTimestamp(timestamp_ns, total_frame_timestamps_);
}

std::uint64_t RtcmCorrectionMonitor::total_frames() const
{
  return total_frames_;
}

std::uint64_t RtcmCorrectionMonitor::valid_frames() const
{
  return valid_frames_;
}

std::uint64_t RtcmCorrectionMonitor::invalid_frames() const
{
  return invalid_frames_;
}

std::size_t RtcmCorrectionMonitor::RetainedTimestampCount() const
{
  std::size_t count = total_frame_timestamps_.size() + valid_frame_timestamps_.size();
  for (const auto& entry : message_type_timestamps_)
  {
    count += entry.second.size();
  }
  for (const auto& entry : msm_constellation_timestamps_)
  {
    count += entry.second.size();
  }
  return count;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::last_frame_timestamp_ns() const
{
  return last_frame_timestamp_ns_;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::first_valid_frame_timestamp_ns() const
{
  return first_valid_frame_timestamp_ns_;
}

std::optional<std::uint16_t> RtcmCorrectionMonitor::station_id() const
{
  return station_id_;
}

const RtcmMessageTypeActivityMap& RtcmCorrectionMonitor::message_type_activity() const
{
  return message_type_activity_;
}

const RtcmMsmConstellationActivityMap& RtcmCorrectionMonitor::msm_constellation_activity() const
{
  return msm_constellation_activity_;
}

const RtcmMsmSummaryActivityMap& RtcmCorrectionMonitor::msm_summary_activity() const
{
  return msm_summary_activity_;
}

std::uint64_t RtcmCorrectionMonitor::MessageCount(const std::uint16_t message_type) const
{
  const auto it = message_type_activity_.find(message_type);
  return it == message_type_activity_.end() ? 0u : it->second.count;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::LastSeenMessageTimestampNs(
    const std::uint16_t message_type) const
{
  const auto it = message_type_activity_.find(message_type);
  return it == message_type_activity_.end() ? std::nullopt : it->second.last_seen_timestamp_ns;
}

std::uint64_t RtcmCorrectionMonitor::MsmConstellationCount(
    const RtcmConstellation constellation) const
{
  const auto it = msm_constellation_activity_.find(constellation);
  return it == msm_constellation_activity_.end() ? 0u : it->second.count;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::LastSeenMsmConstellationTimestampNs(
    const RtcmConstellation constellation) const
{
  const auto it = msm_constellation_activity_.find(constellation);
  return it == msm_constellation_activity_.end() ? std::nullopt : it->second.last_seen_timestamp_ns;
}

bool RtcmCorrectionMonitor::HasSeenBasePositionMessage() const
{
  return seen_base_position_1005_ || seen_base_position_1006_;
}

bool RtcmCorrectionMonitor::HasBaseStationPosition() const
{
  return last_base_station_arp_.has_value();
}

bool RtcmCorrectionMonitor::HasSeenBasePosition1005() const
{
  return seen_base_position_1005_;
}

bool RtcmCorrectionMonitor::HasSeenBasePosition1006() const
{
  return seen_base_position_1006_;
}

bool RtcmCorrectionMonitor::HasSeenGlonassBias1230() const
{
  return seen_glonass_bias_1230_;
}

bool RtcmCorrectionMonitor::HasDecodedGlonassBias1230() const
{
  return last_glonass_code_phase_bias_.has_value();
}

bool RtcmCorrectionMonitor::LastGlonassBias1230Valid() const
{
  return last_glonass_code_phase_bias_.has_value() && last_glonass_code_phase_bias_->valid;
}

bool RtcmCorrectionMonitor::HasSeenAnyMsmMessage() const
{
  return !msm_constellation_activity_.empty();
}

bool RtcmCorrectionMonitor::HasDecodedAnyMsmSummary() const
{
  return last_msm_summary_.has_value();
}

const std::optional<RtcmBaseStationArpRecord>& RtcmCorrectionMonitor::last_base_station_arp() const
{
  return last_base_station_arp_;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::LastBaseStationArpTimestampNs() const
{
  return last_base_station_arp_timestamp_ns_;
}

std::uint64_t RtcmCorrectionMonitor::BaseStationArpDecodeSuccessCount() const
{
  return base_station_arp_decode_success_count_;
}

std::uint64_t RtcmCorrectionMonitor::BaseStationArpDecodeFailureCount() const
{
  return base_station_arp_decode_failure_count_;
}

std::uint64_t RtcmCorrectionMonitor::BaseStationArpMalformedCount() const
{
  return base_station_arp_malformed_count_;
}

const std::optional<RtcmGlonassCodePhaseBiasRecord>&
RtcmCorrectionMonitor::last_glonass_code_phase_bias() const
{
  return last_glonass_code_phase_bias_;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::LastGlonassBias1230TimestampNs() const
{
  return last_glonass_bias_1230_timestamp_ns_;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::LastDecodedGlonassBias1230TimestampNs()
    const
{
  return last_decoded_glonass_bias_1230_timestamp_ns_;
}

std::uint64_t RtcmCorrectionMonitor::GlonassBias1230DecodeSuccessCount() const
{
  return glonass_bias_1230_decode_success_count_;
}

std::uint64_t RtcmCorrectionMonitor::GlonassBias1230DecodeFailureCount() const
{
  return glonass_bias_1230_decode_failure_count_;
}

std::uint64_t RtcmCorrectionMonitor::GlonassBias1230MalformedCount() const
{
  return glonass_bias_1230_malformed_count_;
}

const std::optional<RtcmMsmSummaryRecord>& RtcmCorrectionMonitor::last_msm_summary() const
{
  return last_msm_summary_;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::LastMsmTimestampNs() const
{
  return last_msm_timestamp_ns_;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::LastDecodedMsmTimestampNs() const
{
  return last_decoded_msm_timestamp_ns_;
}

std::uint64_t RtcmCorrectionMonitor::MsmDecodeSuccessCount() const
{
  return msm_decode_success_count_;
}

std::uint64_t RtcmCorrectionMonitor::MsmDecodeFailureCount() const
{
  return msm_decode_failure_count_;
}

std::uint64_t RtcmCorrectionMonitor::MsmMalformedCount() const
{
  return msm_malformed_count_;
}

bool RtcmCorrectionMonitor::HasRequiredMessageTypes(
    const std::vector<std::uint16_t>& message_types) const
{
  for (const std::uint16_t message_type : message_types)
  {
    const auto it = semantic_message_type_activity_.find(message_type);
    if (it == semantic_message_type_activity_.end() || it->second.count == 0u)
    {
      return false;
    }
  }

  return true;
}

bool RtcmCorrectionMonitor::HasRequiredCorrectionMessages(
    const RtcmCorrectionHealthOptions& options) const
{
  if (valid_frames_ == 0u || semantic_message_type_activity_.empty())
  {
    return false;
  }

  const auto window_start_timestamp_ns = ComputeObservationWindowStart(options);
  const auto has_message_type = [&](const std::uint16_t message_type)
  {
    const auto semantic_activity = semantic_message_type_activity_.find(message_type);
    const std::uint64_t semantic_count =
        semantic_activity == semantic_message_type_activity_.end()
            ? 0u
            : semantic_activity->second.count;
    const std::optional<ProtocolTimestampNs> semantic_timestamp_ns =
        semantic_activity == semantic_message_type_activity_.end()
            ? std::nullopt
            : semantic_activity->second.last_seen_timestamp_ns;
    if (message_type == 1005u)
    {
      return semantic_count > 0u;
    }
    if (message_type == 1006u)
    {
      return semantic_count > 0u;
    }
    return HasActivityInWindow(semantic_count,
                               semantic_timestamp_ns,
                               window_start_timestamp_ns,
                               options.now_timestamp_ns);
  };
  const auto has_constellation = [&](const RtcmConstellation constellation)
  {
    const auto semantic_activity = semantic_msm_constellation_activity_.find(constellation);
    return HasActivityInWindow(
        semantic_activity == semantic_msm_constellation_activity_.end()
            ? 0u
            : semantic_activity->second.count,
        semantic_activity == semantic_msm_constellation_activity_.end()
            ? std::nullopt
            : semantic_activity->second.last_seen_timestamp_ns,
        window_start_timestamp_ns,
        options.now_timestamp_ns);
  };

  for (const std::uint16_t message_type : options.required_message_types)
  {
    if (!has_message_type(message_type))
    {
      return false;
    }
  }

  for (const RtcmConstellation constellation : options.required_msm_constellations)
  {
    if (!has_constellation(constellation))
    {
      return false;
    }
  }

  if (options.require_any_msm)
  {
    bool seen_recent_msm = false;
    for (const auto& entry : semantic_msm_constellation_activity_)
    {
      if (HasActivityInWindow(entry.second.count,
                              entry.second.last_seen_timestamp_ns,
                              window_start_timestamp_ns,
                              options.now_timestamp_ns))
      {
        seen_recent_msm = true;
        break;
      }
    }
    if (!seen_recent_msm)
    {
      return false;
    }
  }

  const auto semantic_1005 = semantic_message_type_activity_.find(1005u);
  const auto semantic_1006 = semantic_message_type_activity_.find(1006u);
  const bool has_semantic_base_position =
      (semantic_1005 != semantic_message_type_activity_.end() &&
       semantic_1005->second.count > 0u) ||
      (semantic_1006 != semantic_message_type_activity_.end() &&
       semantic_1006->second.count > 0u);
  if (options.require_base_position && !has_semantic_base_position)
  {
    return false;
  }

  if (options.require_glonass_bias && !has_message_type(1230u))
  {
    return false;
  }

  return true;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::AgeSinceLastFrameNs(
    const ProtocolTimestampNs now_timestamp_ns) const
{
  return ComputeAgeSince(last_frame_timestamp_ns_, now_timestamp_ns);
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::AgeSinceMessageTypeNs(
    const std::uint16_t message_type,
    const ProtocolTimestampNs now_timestamp_ns) const
{
  return ComputeAgeSince(LastSeenMessageTimestampNs(message_type), now_timestamp_ns);
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::AgeSinceMsmConstellationNs(
    const RtcmConstellation constellation,
    const ProtocolTimestampNs now_timestamp_ns) const
{
  return ComputeAgeSince(LastSeenMsmConstellationTimestampNs(constellation), now_timestamp_ns);
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::AgeSinceBaseStationArpNs(
    const ProtocolTimestampNs now_timestamp_ns) const
{
  return ComputeAgeSince(last_base_station_arp_timestamp_ns_, now_timestamp_ns);
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::AgeSinceGlonassBias1230Ns(
    const ProtocolTimestampNs now_timestamp_ns) const
{
  return ComputeAgeSince(last_glonass_bias_1230_timestamp_ns_, now_timestamp_ns);
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::AgeSinceLastMsmNs(
    const ProtocolTimestampNs now_timestamp_ns) const
{
  return ComputeAgeSince(last_msm_timestamp_ns_, now_timestamp_ns);
}

std::optional<double> RtcmCorrectionMonitor::TotalFrameRateHz(
    const ProtocolTimestampNs window_end_timestamp_ns,
    const ProtocolTimestampNs window_duration_ns) const
{
  return ComputeRateHz(total_frame_timestamps_, window_end_timestamp_ns, window_duration_ns);
}

std::optional<double> RtcmCorrectionMonitor::ValidFrameRateHz(
    const ProtocolTimestampNs window_end_timestamp_ns,
    const ProtocolTimestampNs window_duration_ns) const
{
  return ComputeRateHz(valid_frame_timestamps_, window_end_timestamp_ns, window_duration_ns);
}

std::optional<double> RtcmCorrectionMonitor::MessageRateHz(
    const std::uint16_t message_type,
    const ProtocolTimestampNs window_end_timestamp_ns,
    const ProtocolTimestampNs window_duration_ns) const
{
  const auto it = message_type_timestamps_.find(message_type);
  if (it == message_type_timestamps_.end())
  {
    return std::nullopt;
  }

  return ComputeRateHz(it->second, window_end_timestamp_ns, window_duration_ns);
}

std::optional<double> RtcmCorrectionMonitor::MsmConstellationRateHz(
    const RtcmConstellation constellation,
    const ProtocolTimestampNs window_end_timestamp_ns,
    const ProtocolTimestampNs window_duration_ns) const
{
  const auto it = msm_constellation_timestamps_.find(constellation);
  if (it == msm_constellation_timestamps_.end())
  {
    return std::nullopt;
  }

  return ComputeRateHz(it->second, window_end_timestamp_ns, window_duration_ns);
}

void RtcmCorrectionMonitor::RecordValidFrameMessage(
    const RtcmMessageInfo& info,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  if (!first_valid_frame_timestamp_ns_.has_value() && timestamp_ns.has_value())
  {
    first_valid_frame_timestamp_ns_ = timestamp_ns;
  }

  ++valid_frames_;
  AppendTimestamp(timestamp_ns, valid_frame_timestamps_);

  RtcmCorrectionActivityStats& message_stats = message_type_activity_[info.message_type];
  ++message_stats.count;
  UpdateLatestTimestamp(timestamp_ns, message_stats.last_seen_timestamp_ns);
  AppendTimestamp(timestamp_ns, message_type_timestamps_[info.message_type]);

  if (info.is_station_arp)
  {
    seen_base_position_1005_ = seen_base_position_1005_ || info.message_type == 1005u;
    seen_base_position_1006_ = seen_base_position_1006_ || info.message_type == 1006u;
  }

  if (info.is_glonass_bias)
  {
    seen_glonass_bias_1230_ = true;
    UpdateLatestTimestamp(timestamp_ns, last_glonass_bias_1230_timestamp_ns_);
  }

  if (info.is_msm)
  {
    RtcmCorrectionActivityStats& constellation_stats =
        msm_constellation_activity_[info.msm_constellation];
    ++constellation_stats.count;
    UpdateLatestTimestamp(timestamp_ns, constellation_stats.last_seen_timestamp_ns);
    AppendTimestamp(timestamp_ns, msm_constellation_timestamps_[info.msm_constellation]);
    UpdateLatestTimestamp(timestamp_ns, last_msm_timestamp_ns_);
  }
}

void RtcmCorrectionMonitor::RecordSemanticMessage(
    const RtcmMessageInfo& info,
    const std::optional<ProtocolTimestampNs> timestamp_ns)
{
  RtcmCorrectionActivityStats& message_stats =
      semantic_message_type_activity_[info.message_type];
  ++message_stats.count;
  UpdateLatestTimestamp(timestamp_ns, message_stats.last_seen_timestamp_ns);

  if (info.is_msm)
  {
    RtcmCorrectionActivityStats& constellation_stats =
        semantic_msm_constellation_activity_[info.msm_constellation];
    ++constellation_stats.count;
    UpdateLatestTimestamp(timestamp_ns, constellation_stats.last_seen_timestamp_ns);
  }
}

void RtcmCorrectionMonitor::PrepareStationOwnership(const std::uint16_t station_id)
{
  bool unowned_decoded_state_from_other_station = false;
  if (!station_id_.has_value())
  {
    unowned_decoded_state_from_other_station =
        last_glonass_code_phase_bias_.has_value() &&
        last_glonass_code_phase_bias_->station_id != station_id;
    for (const auto& entry : msm_summary_activity_)
    {
      if (entry.second.last_summary.has_value() &&
          entry.second.last_summary->station_id != station_id)
      {
        unowned_decoded_state_from_other_station = true;
        break;
      }
    }
  }

  if ((station_id_.has_value() && *station_id_ != station_id) ||
      unowned_decoded_state_from_other_station)
  {
    Reset();
  }
  station_id_ = station_id;
}

universal_gnss::GnssHealthSummary BuildRtcmCorrectionHealth(
    const RtcmCorrectionMonitor& monitor,
    const RtcmCorrectionHealthOptions& options)
{
  universal_gnss::GnssHealthSummary summary;
  summary.parser_healthy = monitor.invalid_frames() == 0u &&
                           monitor.BaseStationArpMalformedCount() == 0u &&
                           monitor.GlonassBias1230MalformedCount() == 0u &&
                           monitor.MsmMalformedCount() == 0u;

  if (monitor.BaseStationArpMalformedCount() > 0u)
  {
    const auto timestamp_1005 = monitor.LastSeenMessageTimestampNs(1005u);
    const auto timestamp_1006 = monitor.LastSeenMessageTimestampNs(1006u);
    const auto malformed_timestamp_ns =
        !timestamp_1005.has_value()
            ? timestamp_1006
            : (!timestamp_1006.has_value() || *timestamp_1005 >= *timestamp_1006
                   ? timestamp_1005
                   : timestamp_1006);
    summary.AddEvent({universal_gnss::GnssDiagnosticSeverity::kWarning,
                      universal_gnss::GnssDiagnosticCategory::kParser,
                      "rtcm.base_station_arp_malformed",
                      "Malformed RTCM 1005/1006 base-station payloads were observed",
                      malformed_timestamp_ns,
                      std::string("rtcm_correction_monitor")});
  }

  if (monitor.GlonassBias1230MalformedCount() > 0u)
  {
    summary.AddEvent({universal_gnss::GnssDiagnosticSeverity::kWarning,
                      universal_gnss::GnssDiagnosticCategory::kParser,
                      "rtcm.1230_malformed",
                      "Malformed RTCM 1230 GLONASS code-phase bias payloads were observed",
                      monitor.LastGlonassBias1230TimestampNs(),
                      std::string("rtcm_correction_monitor")});
  }

  if (monitor.HasDecodedGlonassBias1230() && !monitor.LastGlonassBias1230Valid())
  {
    // Informational only: GLONASS code-phase bias is optional for RTK (see
    // ConfigurePortableRtkCorrectionRequirements). A caster that transmits 1230
    // with the validity indicator cleared is signalling "no GLONASS bias
    // available", which is a normal, expected condition — not a correction fault.
    summary.AddEvent({universal_gnss::GnssDiagnosticSeverity::kInfo,
                      universal_gnss::GnssDiagnosticCategory::kCorrection,
                      "rtcm.1230_not_valid",
                      "The latest RTCM 1230 GLONASS code-phase bias message is not marked valid",
                      monitor.LastDecodedGlonassBias1230TimestampNs(),
                      std::string("rtcm_correction_monitor")});
  }

  if (monitor.MsmMalformedCount() > 0u)
  {
    summary.AddEvent({universal_gnss::GnssDiagnosticSeverity::kWarning,
                      universal_gnss::GnssDiagnosticCategory::kParser,
                      "rtcm.msm_malformed",
                      "Malformed RTCM MSM payloads were observed",
                      monitor.LastMsmTimestampNs(),
                      std::string("rtcm_correction_monitor")});
  }

  const bool has_required_messages = monitor.HasRequiredCorrectionMessages(options);
  const bool within_startup_grace =
      !has_required_messages && IsWithinStartupGrace(monitor, options);
  if (!has_required_messages && !within_startup_grace)
  {
    summary.AddEvent({universal_gnss::GnssDiagnosticSeverity::kError,
                      universal_gnss::GnssDiagnosticCategory::kCorrection,
                      "rtcm.required_messages_missing",
                      "Required RTCM correction messages have not been observed",
                      monitor.last_frame_timestamp_ns(),
                      std::string("rtcm_correction_monitor")});
  }
  else if (within_startup_grace)
  {
    summary.AddEvent({universal_gnss::GnssDiagnosticSeverity::kInfo,
                      universal_gnss::GnssDiagnosticCategory::kCorrection,
                      "rtcm.required_messages_pending",
                      "Waiting for the complete RTCM correction message set during startup grace",
                      monitor.last_frame_timestamp_ns(),
                      std::string("rtcm_correction_monitor")});
  }

  if (!options.now_timestamp_ns.has_value() || !monitor.last_frame_timestamp_ns().has_value())
  {
    summary.AddEvent({universal_gnss::GnssDiagnosticSeverity::kUnknown,
                      universal_gnss::GnssDiagnosticCategory::kTiming,
                      "rtcm.freshness_unknown",
                      "RTCM correction freshness is unknown because timestamps are unavailable",
                      monitor.last_frame_timestamp_ns(),
                      std::string("rtcm_correction_monitor")});
    return summary;
  }

  const std::optional<ProtocolTimestampNs> age_since_last_frame_ns =
      monitor.AgeSinceLastFrameNs(*options.now_timestamp_ns);
  if (!age_since_last_frame_ns.has_value())
  {
    summary.AddEvent({universal_gnss::GnssDiagnosticSeverity::kUnknown,
                      universal_gnss::GnssDiagnosticCategory::kTiming,
                      "rtcm.freshness_unknown",
                      "RTCM correction freshness is unknown because timestamps are unavailable",
                      monitor.last_frame_timestamp_ns(),
                      std::string("rtcm_correction_monitor")});
    return summary;
  }

  if (options.stale_after_ns > 0 && *age_since_last_frame_ns > options.stale_after_ns)
  {
    summary.stale_data = true;
    summary.AddEvent({universal_gnss::GnssDiagnosticSeverity::kWarning,
                      universal_gnss::GnssDiagnosticCategory::kTiming,
                      "rtcm.stream_stale",
                      "RTCM correction stream is stale",
                      monitor.last_frame_timestamp_ns(),
                      std::string("rtcm_correction_monitor")});
    return summary;
  }

  if (!has_required_messages)
  {
    return summary;
  }

  summary.correction_available = has_required_messages;
  summary.AddEvent({universal_gnss::GnssDiagnosticSeverity::kOk,
                    universal_gnss::GnssDiagnosticCategory::kCorrection,
                    "rtcm.stream_active",
                    "RTCM correction stream is active",
                    monitor.last_frame_timestamp_ns(),
                    std::string("rtcm_correction_monitor")});
  return summary;
}

RtcmSemanticObservations BuildRtcmSemanticObservations(
    const RtcmCorrectionMonitor& monitor,
    const std::optional<ProtocolTimestampNs> now_timestamp_ns)
{
  RtcmSemanticObservations observations;

  RtcmSemanticObservation base_station_arp;
  base_station_arp.name = "base_station_arp";
  base_station_arp.message_type =
      monitor.last_base_station_arp().has_value()
          ? monitor.last_base_station_arp()->message_type
          : (monitor.HasSeenBasePosition1006() ? 1006u
             : monitor.HasSeenBasePosition1005() ? 1005u
                                                 : 0u);
  base_station_arp.seen = monitor.HasSeenBasePositionMessage();
  base_station_arp.decoded = monitor.last_base_station_arp().has_value();
  base_station_arp.valid = base_station_arp.decoded;
  base_station_arp.decode_success_count = monitor.BaseStationArpDecodeSuccessCount();
  base_station_arp.decode_failure_count = monitor.BaseStationArpDecodeFailureCount();
  base_station_arp.malformed_count = monitor.BaseStationArpMalformedCount();
  base_station_arp.last_seen_timestamp_ns = monitor.LastSeenMessageTimestampNs(1005u);
  UpdateLatestTimestamp(
      monitor.LastSeenMessageTimestampNs(1006u), base_station_arp.last_seen_timestamp_ns);
  base_station_arp.last_decoded_timestamp_ns = monitor.LastBaseStationArpTimestampNs();
  if (now_timestamp_ns.has_value())
  {
    base_station_arp.age_ns = monitor.AgeSinceBaseStationArpNs(*now_timestamp_ns);
  }
  if (monitor.last_base_station_arp().has_value())
  {
    const auto& arp = *monitor.last_base_station_arp();
    base_station_arp.fields.push_back({"station_id", std::to_string(arp.station_id)});
    base_station_arp.fields.push_back({"ecef_x_m", FormatDoubleValue(arp.ecef_x_m)});
    base_station_arp.fields.push_back({"ecef_y_m", FormatDoubleValue(arp.ecef_y_m)});
    base_station_arp.fields.push_back({"ecef_z_m", FormatDoubleValue(arp.ecef_z_m)});
    if (arp.antenna_height_m.has_value())
    {
      base_station_arp.fields.push_back(
          {"antenna_height_m", FormatDoubleValue(*arp.antenna_height_m)});
    }
  }
  observations.push_back(std::move(base_station_arp));

  RtcmSemanticObservation glonass_bias;
  glonass_bias.name = "glonass_code_phase_bias";
  glonass_bias.message_type = 1230u;
  glonass_bias.seen = monitor.HasSeenGlonassBias1230();
  glonass_bias.decoded = monitor.HasDecodedGlonassBias1230();
  glonass_bias.valid = monitor.LastGlonassBias1230Valid();
  glonass_bias.decode_success_count = monitor.GlonassBias1230DecodeSuccessCount();
  glonass_bias.decode_failure_count = monitor.GlonassBias1230DecodeFailureCount();
  glonass_bias.malformed_count = monitor.GlonassBias1230MalformedCount();
  glonass_bias.last_seen_timestamp_ns = monitor.LastGlonassBias1230TimestampNs();
  glonass_bias.last_decoded_timestamp_ns = monitor.LastDecodedGlonassBias1230TimestampNs();
  if (now_timestamp_ns.has_value())
  {
    glonass_bias.age_ns = monitor.AgeSinceGlonassBias1230Ns(*now_timestamp_ns);
  }
  if (monitor.last_glonass_code_phase_bias().has_value())
  {
    const auto& record = *monitor.last_glonass_code_phase_bias();
    glonass_bias.fields.push_back({"station_id", std::to_string(record.station_id)});
    glonass_bias.fields.push_back(
        {"code_phase_bias_indicator", record.code_phase_bias_indicator ? "true" : "false"});
    glonass_bias.fields.push_back({"signal_mask", FormatMaskValue(record.signal_mask)});
    if (record.l1_ca_bias_m.has_value())
    {
      glonass_bias.fields.push_back({"l1_ca_bias_m", FormatDoubleValue(*record.l1_ca_bias_m)});
    }
    if (record.l1_p_bias_m.has_value())
    {
      glonass_bias.fields.push_back({"l1_p_bias_m", FormatDoubleValue(*record.l1_p_bias_m)});
    }
    if (record.l2_ca_bias_m.has_value())
    {
      glonass_bias.fields.push_back({"l2_ca_bias_m", FormatDoubleValue(*record.l2_ca_bias_m)});
    }
    if (record.l2_p_bias_m.has_value())
    {
      glonass_bias.fields.push_back({"l2_p_bias_m", FormatDoubleValue(*record.l2_p_bias_m)});
    }
  }
  observations.push_back(std::move(glonass_bias));

  RtcmSemanticObservation msm_summary;
  msm_summary.name = "msm_summary";
  msm_summary.message_type =
      monitor.last_msm_summary().has_value() ? monitor.last_msm_summary()->message_type
                                             : FindLatestSeenMsmMessageType(monitor);
  msm_summary.seen = monitor.HasSeenAnyMsmMessage();
  msm_summary.decoded = monitor.HasDecodedAnyMsmSummary();
  msm_summary.valid = msm_summary.decoded;
  msm_summary.decode_success_count = monitor.MsmDecodeSuccessCount();
  msm_summary.decode_failure_count = monitor.MsmDecodeFailureCount();
  msm_summary.malformed_count = monitor.MsmMalformedCount();
  msm_summary.last_seen_timestamp_ns = monitor.LastMsmTimestampNs();
  msm_summary.last_decoded_timestamp_ns = monitor.LastDecodedMsmTimestampNs();
  if (now_timestamp_ns.has_value())
  {
    msm_summary.age_ns = monitor.AgeSinceLastMsmNs(*now_timestamp_ns);
  }
  if (!monitor.msm_constellation_activity().empty())
  {
    std::vector<std::string> constellations_seen;
    for (const auto& entry : monitor.msm_constellation_activity())
    {
      constellations_seen.push_back(DescribeRtcmConstellation(entry.first));
    }
    msm_summary.fields.push_back({"constellations_seen", JoinStrings(constellations_seen)});
  }
  if (monitor.last_msm_summary().has_value())
  {
    const auto& record = *monitor.last_msm_summary();
    msm_summary.fields.push_back({"station_id", std::to_string(record.station_id)});
    msm_summary.fields.push_back({"constellation", DescribeRtcmConstellation(record.constellation)});
    msm_summary.fields.push_back(
        {"msm_variant", std::to_string(static_cast<unsigned int>(record.msm_variant))});
    msm_summary.fields.push_back(
        {"satellite_count", std::to_string(static_cast<unsigned int>(record.satellite_count))});
    msm_summary.fields.push_back(
        {"signal_count", std::to_string(static_cast<unsigned int>(record.signal_count))});
    msm_summary.fields.push_back(
        {"cell_count", std::to_string(static_cast<unsigned int>(record.cell_count))});
  }
  observations.push_back(std::move(msm_summary));

  for (const auto& entry : monitor.message_type_activity())
  {
    if (!IsRtcmMsmMessage(entry.first))
    {
      continue;
    }

    RtcmSemanticObservation observation;
    observation.name = BuildMsmObservationName(
        GetRtcmMsmConstellation(entry.first), GetRtcmMsmVariant(entry.first));
    observation.message_type = entry.first;
    observation.seen = entry.second.count > 0u;
    observation.valid = false;
    observation.last_seen_timestamp_ns = entry.second.last_seen_timestamp_ns;
    if (now_timestamp_ns.has_value())
    {
      observation.age_ns = monitor.AgeSinceMessageTypeNs(entry.first, *now_timestamp_ns);
    }

    const auto stats_it = monitor.msm_summary_activity().find(entry.first);
    if (stats_it != monitor.msm_summary_activity().end())
    {
      observation.decoded = stats_it->second.last_summary.has_value();
      observation.valid = observation.decoded;
      observation.decode_success_count = stats_it->second.decode_success_count;
      observation.decode_failure_count = stats_it->second.decode_failure_count;
      observation.malformed_count = stats_it->second.malformed_count;
      observation.last_decoded_timestamp_ns = stats_it->second.last_decoded_timestamp_ns;
      if (stats_it->second.last_summary.has_value())
      {
        const auto& record = *stats_it->second.last_summary;
        observation.fields.push_back({"station_id", std::to_string(record.station_id)});
        observation.fields.push_back(
            {"constellation", DescribeRtcmConstellation(record.constellation)});
        observation.fields.push_back(
            {"msm_variant", std::to_string(static_cast<unsigned int>(record.msm_variant))});
        observation.fields.push_back(
            {"satellite_count",
             std::to_string(static_cast<unsigned int>(record.satellite_count))});
        observation.fields.push_back(
            {"signal_count", std::to_string(static_cast<unsigned int>(record.signal_count))});
        observation.fields.push_back(
            {"cell_count", std::to_string(static_cast<unsigned int>(record.cell_count))});
      }
    }
    else
    {
      observation.decoded = false;
    }

    observations.push_back(std::move(observation));
  }

  return observations;
}

}  // namespace universal_gnss_protocols
