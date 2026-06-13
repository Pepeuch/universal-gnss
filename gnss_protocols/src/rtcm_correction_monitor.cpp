#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"

#include <cstddef>
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
                     std::vector<ProtocolTimestampNs>& timestamps)
{
  if (timestamp_ns.has_value())
  {
    timestamps.push_back(*timestamp_ns);
  }
}

std::size_t CountTimestampsInWindow(const std::vector<ProtocolTimestampNs>& timestamps,
                                    const ProtocolTimestampNs window_end_timestamp_ns,
                                    const ProtocolTimestampNs window_duration_ns)
{
  const ProtocolTimestampNs window_start_timestamp_ns =
      window_end_timestamp_ns - window_duration_ns;

  std::size_t count = 0;
  for (const ProtocolTimestampNs timestamp_ns : timestamps)
  {
    if (timestamp_ns >= window_start_timestamp_ns && timestamp_ns <= window_end_timestamp_ns)
    {
      ++count;
    }
  }

  return count;
}

std::optional<double> ComputeRateHz(const std::vector<ProtocolTimestampNs>& timestamps,
                                    const ProtocolTimestampNs window_end_timestamp_ns,
                                    const ProtocolTimestampNs window_duration_ns)
{
  if (window_duration_ns <= 0)
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

  return now_timestamp_ns - *last_seen_timestamp_ns;
}

bool HasSeenSince(const std::optional<ProtocolTimestampNs>& last_seen_timestamp_ns,
                  const std::optional<ProtocolTimestampNs>& window_start_timestamp_ns)
{
  if (!last_seen_timestamp_ns.has_value())
  {
    return false;
  }

  return !window_start_timestamp_ns.has_value() ||
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

  return *options.now_timestamp_ns - *first_valid_timestamp_ns < options.startup_grace_ns;
}

}  // namespace

void ConfigurePortableRtkCorrectionRequirements(RtcmCorrectionHealthOptions& options)
{
  options.required_msm_constellations = {
      RtcmConstellation::kGps,
      RtcmConstellation::kGlonass,
      RtcmConstellation::kGalileo,
      RtcmConstellation::kBeiDou,
  };
  options.require_any_msm = false;
  options.require_base_position = true;
  options.require_glonass_bias = true;
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
  message_type_timestamps_.clear();
  msm_constellation_timestamps_.clear();
  total_frame_timestamps_.clear();
  valid_frame_timestamps_.clear();
  seen_base_position_1005_ = false;
  seen_base_position_1006_ = false;
  seen_glonass_bias_1230_ = false;
  last_base_station_arp_.reset();
  last_base_station_arp_timestamp_ns_.reset();
}

void RtcmCorrectionMonitor::ObserveFrame(const RtcmFrame& frame)
{
  ++total_frames_;
  UpdateLatestTimestamp(frame.timestamp_ns, last_frame_timestamp_ns_);
  AppendTimestamp(frame.timestamp_ns, total_frame_timestamps_);

  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    ++invalid_frames_;
    return;
  }

  const auto parsed_info = ParseRtcmMessageInfo(frame);
  if (parsed_info.status != ParserStatus::kRecordReady || !parsed_info.record.has_value())
  {
    ++invalid_frames_;
    return;
  }

  if (parsed_info.record->is_station_arp)
  {
    const auto parsed_arp = ParseRtcmBaseStationArp(frame);
    if (parsed_arp.status == ParserStatus::kRecordReady && parsed_arp.record.has_value())
    {
      last_base_station_arp_ = *parsed_arp.record;
      UpdateLatestTimestamp(frame.timestamp_ns, last_base_station_arp_timestamp_ns_);
    }
  }

  RecordValidMessage(*parsed_info.record, frame.timestamp_ns);
}

void RtcmCorrectionMonitor::ObserveMessage(const RtcmMessageInfo& info,
                                           std::optional<ProtocolTimestampNs> timestamp_ns)
{
  ++total_frames_;
  UpdateLatestTimestamp(timestamp_ns, last_frame_timestamp_ns_);
  AppendTimestamp(timestamp_ns, total_frame_timestamps_);
  RecordValidMessage(info, timestamp_ns);
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

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::last_frame_timestamp_ns() const
{
  return last_frame_timestamp_ns_;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::first_valid_frame_timestamp_ns() const
{
  return first_valid_frame_timestamp_ns_;
}

const RtcmMessageTypeActivityMap& RtcmCorrectionMonitor::message_type_activity() const
{
  return message_type_activity_;
}

const RtcmMsmConstellationActivityMap& RtcmCorrectionMonitor::msm_constellation_activity() const
{
  return msm_constellation_activity_;
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

bool RtcmCorrectionMonitor::HasSeenAnyMsmMessage() const
{
  return !msm_constellation_activity_.empty();
}

const std::optional<RtcmBaseStationArpRecord>& RtcmCorrectionMonitor::last_base_station_arp() const
{
  return last_base_station_arp_;
}

std::optional<ProtocolTimestampNs> RtcmCorrectionMonitor::LastBaseStationArpTimestampNs() const
{
  return last_base_station_arp_timestamp_ns_;
}

bool RtcmCorrectionMonitor::HasRequiredMessageTypes(
    const std::vector<std::uint16_t>& message_types) const
{
  for (const std::uint16_t message_type : message_types)
  {
    if (MessageCount(message_type) == 0u)
    {
      return false;
    }
  }

  return true;
}

bool RtcmCorrectionMonitor::HasRequiredCorrectionMessages(
    const RtcmCorrectionHealthOptions& options) const
{
  if (valid_frames_ == 0u)
  {
    return false;
  }

  const auto window_start_timestamp_ns = ComputeObservationWindowStart(options);
  const auto has_message_type = [&](const std::uint16_t message_type)
  {
    if (!window_start_timestamp_ns.has_value())
    {
      return MessageCount(message_type) > 0u;
    }
    return HasSeenSince(LastSeenMessageTimestampNs(message_type), window_start_timestamp_ns);
  };
  const auto has_constellation = [&](const RtcmConstellation constellation)
  {
    if (!window_start_timestamp_ns.has_value())
    {
      return MsmConstellationCount(constellation) > 0u;
    }
    return HasSeenSince(LastSeenMsmConstellationTimestampNs(constellation),
                        window_start_timestamp_ns);
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
    if (!window_start_timestamp_ns.has_value())
    {
      if (!HasSeenAnyMsmMessage())
      {
        return false;
      }
    }
    else
    {
      bool seen_recent_msm = false;
      for (const auto& entry : msm_constellation_activity_)
      {
        if (HasSeenSince(entry.second.last_seen_timestamp_ns, window_start_timestamp_ns))
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
  }

  if (options.require_base_position)
  {
    if (!window_start_timestamp_ns.has_value())
    {
      if (!HasSeenBasePositionMessage())
      {
        return false;
      }
    }
    else if (!has_message_type(1005u) && !has_message_type(1006u))
    {
      return false;
    }
  }

  if (options.require_glonass_bias)
  {
    if (!window_start_timestamp_ns.has_value())
    {
      if (!HasSeenGlonassBias1230())
      {
        return false;
      }
    }
    else if (!has_message_type(1230u))
    {
      return false;
    }
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

void RtcmCorrectionMonitor::RecordValidMessage(const RtcmMessageInfo& info,
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
  }

  if (info.is_msm)
  {
    RtcmCorrectionActivityStats& constellation_stats =
        msm_constellation_activity_[info.msm_constellation];
    ++constellation_stats.count;
    UpdateLatestTimestamp(timestamp_ns, constellation_stats.last_seen_timestamp_ns);
    AppendTimestamp(timestamp_ns, msm_constellation_timestamps_[info.msm_constellation]);
  }
}

universal_gnss::GnssHealthSummary BuildRtcmCorrectionHealth(
    const RtcmCorrectionMonitor& monitor,
    const RtcmCorrectionHealthOptions& options)
{
  universal_gnss::GnssHealthSummary summary;
  summary.parser_healthy = monitor.invalid_frames() == 0u;

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

}  // namespace universal_gnss_protocols
