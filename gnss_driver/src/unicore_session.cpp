#include "universal_gnss_driver/unicore_session.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/mixed_stream_resync.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/nmea_parser.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/unicore_parser.hpp"

namespace universal_gnss_driver
{

namespace
{

using universal_gnss::ClearOptionalValue;
using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRuntimeState;
using universal_gnss::SetCapability;
using universal_gnss::SetOptionalValue;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaGsvRecord;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::NmeaSentenceFramer;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UnicoreBinaryFrameFramer;
using universal_gnss_protocols::UnicoreBinaryFrame;
using universal_gnss_protocols::UnicoreFrameFramer;
using universal_gnss_protocols::UnicoreFrame;

constexpr std::int64_t kGsvTalkerFreshnessWindowNs = 5'000'000'000ll;

bool IsSupportedNmeaSentenceType(const NmeaSentence& sentence)
{
  return universal_gnss_protocols::IsNmeaSentenceType(sentence, "GSV") ||
         universal_gnss_protocols::IsNmeaSentenceType(sentence, "GGA") ||
         universal_gnss_protocols::IsNmeaGst(sentence);
}

void PruneNmeaGgaFallback(const GnssRuntimeState& current_state,
                          const bool preserve_satellites_used,
                          GnssRuntimeState& update)
{
  if (current_state.fix_type != GnssFixType::kUnknown)
  {
    update.fix_valid = false;
    update.fix_type = GnssFixType::kUnknown;
  }
  if (current_state.latitude_deg.has_value())
  {
    update.latitude_deg.reset();
  }
  if (current_state.longitude_deg.has_value())
  {
    update.longitude_deg.reset();
  }
  if (current_state.altitude_m.has_value())
  {
    update.altitude_m.reset();
  }
  if (current_state.hdop.has_value())
  {
    ClearOptionalValue(update, GnssCapability::kHdop, update.hdop);
  }
  if (!preserve_satellites_used && current_state.satellites_used.has_value())
  {
    ClearOptionalValue(update, GnssCapability::kSatellitesUsed, update.satellites_used);
  }
}

void PruneNmeaGstFallback(const GnssRuntimeState& current_state, GnssRuntimeState& update)
{
  if (current_state.horizontal_accuracy_m.has_value())
  {
    ClearOptionalValue(
        update, GnssCapability::kHorizontalAccuracy, update.horizontal_accuracy_m);
  }
  if (current_state.vertical_accuracy_m.has_value())
  {
    ClearOptionalValue(update, GnssCapability::kVerticalAccuracy, update.vertical_accuracy_m);
  }
}

bool HasFreshMixedNmeaSample(const bool seen,
                             const std::optional<std::int64_t>& last_timestamp_ns,
                             const std::optional<std::int64_t>& update_timestamp_ns)
{
  if (!seen)
  {
    return false;
  }

  if (!update_timestamp_ns.has_value() || !last_timestamp_ns.has_value())
  {
    return true;
  }

  return *update_timestamp_ns >= *last_timestamp_ns &&
         (*update_timestamp_ns - *last_timestamp_ns) <= kGsvTalkerFreshnessWindowNs;
}

template <typename RecordT>
struct ProbeResult
{
  ParserStatus status{ParserStatus::kIdle};
  std::optional<RecordT> record{};
  std::size_t bytes_consumed{0u};
};

template <typename FramerT, typename RecordT>
ProbeResult<RecordT> ProbeAtOffset(FramerT& framer,
                                   const std::vector<UnicoreBufferedByte>& bytes,
                                   const std::size_t start_offset)
{
  framer.Reset();
  for (std::size_t index = start_offset; index < bytes.size(); ++index)
  {
    auto parser_result = framer.PushByte(bytes[index].value, bytes[index].timestamp_ns);
    if (parser_result.status == ParserStatus::kNeedMoreData ||
        parser_result.status == ParserStatus::kIdle)
    {
      continue;
    }

    return ProbeResult<RecordT>{
        parser_result.status,
        std::move(parser_result.record),
        (index - start_offset) + 1u,
    };
  }

  auto finalize_result = framer.Finalize();
  return ProbeResult<RecordT>{
      finalize_result.status,
      std::move(finalize_result.record),
      bytes.size() - start_offset,
  };
}

template <typename ParseFn, typename MapFn>
bool ParseAndMergeRecord(const UnicoreFrame& frame,
                         ParseFn&& parse_fn,
                         MapFn&& map_fn,
                         universal_gnss::GnssRuntimeAggregator& aggregator,
                         UnicoreSessionMetrics& metrics)
{
  const auto parsed = std::forward<ParseFn>(parse_fn)(frame);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics.records_rejected;
    return false;
  }

  ++metrics.records_parsed;
  ++metrics.runtime_observations;
  if (aggregator.Merge(std::forward<MapFn>(map_fn)(*parsed.record)))
  {
    ++metrics.runtime_updates;
    return true;
  }
  return false;
}

template <typename ParseFn, typename MapFn>
bool ParseAndMergeBinaryRecord(const UnicoreBinaryFrame& frame,
                               ParseFn&& parse_fn,
                               MapFn&& map_fn,
                               const bool is_position_observation,
                               universal_gnss::GnssRuntimeAggregator& aggregator,
                               UnicoreSessionMetrics& metrics)
{
  const auto parsed = std::forward<ParseFn>(parse_fn)(frame);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics.records_rejected;
    return false;
  }

  ++metrics.records_parsed;
  ++metrics.runtime_observations;
  if (is_position_observation)
  {
    ++metrics.position_observations;
  }
  if (aggregator.Merge(std::forward<MapFn>(map_fn)(*parsed.record)))
  {
    ++metrics.runtime_updates;
    return true;
  }
  return false;
}

template <typename ParseFn>
void ParseRecordOnly(const UnicoreFrame& frame, ParseFn&& parse_fn, UnicoreSessionMetrics& metrics)
{
  const auto parsed = std::forward<ParseFn>(parse_fn)(frame);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics.records_rejected;
    return;
  }

  ++metrics.records_parsed;
}

void ParseRtcmStatusRecord(const UnicoreFrame& frame, UnicoreSessionMetrics& metrics)
{
  const auto parsed = universal_gnss_protocols::ParseUnicoreRtcmStatus(frame);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics.records_rejected;
    return;
  }

  const auto& record = *parsed.record;
  ++metrics.records_parsed;
  ++metrics.receiver_rtcm_status_messages_seen;
  metrics.receiver_rtcm_status_message_count = record.message_count;
  metrics.receiver_last_rtcm_message_type = record.message_type;
  metrics.receiver_last_rtcm_base_station_id = record.base_station_id;
  metrics.receiver_last_rtcm_satellites_in_message = record.satellites_in_message;
}

bool IsSupportedRecordName(const std::string_view name)
{
  return name == "PVTSLNA" || name == "BESTNAVA" || name == "RTKSTATUSA" || name == "RTCMSTATUSA" ||
         name == "BESTSATA" || name == "SATSINFOA" || name == "JAMSTATUSA" ||
         name == "FREQJAMSTATUSA" || name == "HWSTATUSA" || name == "AGCA";
}

bool IsSupportedBinaryMessageId(const std::uint16_t message_id)
{
  return message_id == 2118u || message_id == 1021u;
}

}  // namespace

UnicoreSession::UnicoreSession(UnicoreSessionConfig config) : config_(config) {}

void UnicoreSession::FeedBytes(const std::uint8_t* data,
                               const std::size_t size,
                               const std::optional<std::int64_t> timestamp_ns)
{
  if (data == nullptr || size == 0u)
  {
    return;
  }

  metrics_.bytes_seen += size;
  for (std::size_t i = 0u; i < size; ++i)
  {
    buffer_.push_back(UnicoreBufferedByte{data[i], timestamp_ns});
  }
  ProcessBufferedData(false);
}

void UnicoreSession::FeedBytes(const std::vector<std::uint8_t>& bytes,
                               const std::optional<std::int64_t> timestamp_ns)
{
  FeedBytes(bytes.data(), bytes.size(), timestamp_ns);
}

void UnicoreSession::FeedString(const std::string_view text,
                                const std::optional<std::int64_t> timestamp_ns)
{
  FeedBytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), timestamp_ns);
}

void UnicoreSession::Finalize()
{
  ProcessBufferedData(true);
}

void UnicoreSession::Reset()
{
  buffer_.clear();
  aggregator_.Reset();
  metrics_ = UnicoreSessionMetrics{};
  ascii_seen_valid_record_ = false;
  binary_seen_valid_frame_ = false;
  ascii_startup_malformed_suppressed_ = false;
  binary_startup_malformed_suppressed_ = false;
  seen_valid_nmea_gga_ = false;
  seen_valid_nmea_gsv_ = false;
  last_nmea_gga_timestamp_ns_.reset();
  last_nmea_gsv_timestamp_ns_.reset();
  gsv_talker_states_.clear();
}

const universal_gnss::GnssRuntimeState& UnicoreSession::current_state() const
{
  return aggregator_.state();
}

const UnicoreSessionMetrics& UnicoreSession::metrics() const
{
  return metrics_;
}

const UnicoreSessionConfig& UnicoreSession::config() const
{
  return config_;
}

void UnicoreSession::ProcessBufferedData(const bool finalizing)
{
  std::size_t offset = 0u;
  while (offset < buffer_.size())
  {
    std::size_t next_offset = offset;
    bool keep_tail = false;
    bool consumed = false;

    const std::uint8_t byte = buffer_[offset].value;
    if (byte == '$' || byte == '!')
    {
      consumed = ConsumeNmeaAtOffset(offset, finalizing, next_offset, keep_tail);
    }
    else if (byte == '#' || byte == '%')
    {
      consumed = ConsumeAsciiAtOffset(offset, finalizing, next_offset, keep_tail);
    }
    else if (byte == universal_gnss_protocols::kUnicoreBinarySync1)
    {
      consumed = ConsumeBinaryAtOffset(offset, finalizing, next_offset, keep_tail);
    }

    if (!consumed)
    {
      next_offset = offset + 1u;
    }

    offset = next_offset;
    if (keep_tail)
    {
      break;
    }
  }

  if (offset > 0u)
  {
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
  }
}

bool UnicoreSession::ConsumeNmeaAtOffset(const std::size_t start_offset,
                                         const bool finalizing,
                                         std::size_t& next_offset,
                                         bool& keep_tail)
{
  if (const auto resync_offset = universal_gnss_protocols::FindEmbeddedMixedRecordResyncOffset(
          buffer_.size(),
          [&](const std::size_t index) { return buffer_[index].value; },
          start_offset);
      resync_offset.has_value())
  {
    if (!ShouldSuppressStartupAsciiMalformed())
    {
      ++metrics_.malformed_lines;
    }
    next_offset = *resync_offset;
    return true;
  }

  NmeaSentenceFramer framer(config_.max_frame_length_bytes);
  const ProbeResult<NmeaSentence> probe =
      ProbeAtOffset<NmeaSentenceFramer, NmeaSentence>(framer, buffer_, start_offset);

  if (probe.status == ParserStatus::kRecordReady && probe.record.has_value())
  {
    ++metrics_.lines_seen;
    if (probe.record->checksum_status != ChecksumStatus::kValid)
    {
      ++metrics_.records_rejected;
    }
    else
    {
      ascii_seen_valid_record_ = true;
      ++metrics_.ascii_records_seen;
      HandleNmeaSentence(*probe.record);
    }
    next_offset = start_offset + probe.bytes_consumed;
    return true;
  }

  if (probe.status == ParserStatus::kTruncated)
  {
    if (finalizing)
    {
      ++metrics_.malformed_lines;
      next_offset = buffer_.size();
    }
    else
    {
      keep_tail = true;
      next_offset = start_offset;
    }
    return true;
  }

  if (probe.status == ParserStatus::kInvalidData || probe.status == ParserStatus::kOverflow)
  {
    if (!ShouldSuppressStartupAsciiMalformed())
    {
      ++metrics_.malformed_lines;
    }
  }

  next_offset = start_offset + 1u;
  return true;
}

bool UnicoreSession::ConsumeAsciiAtOffset(const std::size_t start_offset,
                                          const bool finalizing,
                                          std::size_t& next_offset,
                                          bool& keep_tail)
{
  if (const auto resync_offset = universal_gnss_protocols::FindEmbeddedMixedRecordResyncOffset(
          buffer_.size(),
          [&](const std::size_t index) { return buffer_[index].value; },
          start_offset);
      resync_offset.has_value())
  {
    if (!ShouldSuppressStartupAsciiMalformed())
    {
      ++metrics_.malformed_lines;
    }
    next_offset = *resync_offset;
    return true;
  }

  UnicoreFrameFramer framer(config_.max_frame_length_bytes);
  const ProbeResult<UnicoreFrame> probe =
      ProbeAtOffset<UnicoreFrameFramer, UnicoreFrame>(framer, buffer_, start_offset);

  if (probe.status == ParserStatus::kRecordReady && probe.record.has_value())
  {
    ++metrics_.lines_seen;
    ascii_seen_valid_record_ = true;
    if (!probe.record->message_name.empty())
    {
      ++metrics_.ascii_records_seen;
    }
    HandleFrame(*probe.record);
    next_offset = start_offset + probe.bytes_consumed;
    return true;
  }

  if (probe.status == ParserStatus::kTruncated)
  {
    if (finalizing)
    {
      ++metrics_.malformed_lines;
      next_offset = buffer_.size();
    }
    else
    {
      keep_tail = true;
      next_offset = start_offset;
    }
    return true;
  }

  if (probe.status == ParserStatus::kInvalidData || probe.status == ParserStatus::kOverflow)
  {
    if (!ShouldSuppressStartupAsciiMalformed())
    {
      ++metrics_.malformed_lines;
    }
  }

  next_offset = start_offset + 1u;
  return true;
}

bool UnicoreSession::ConsumeBinaryAtOffset(const std::size_t start_offset,
                                           const bool finalizing,
                                           std::size_t& next_offset,
                                           bool& keep_tail)
{
  UnicoreBinaryFrameFramer framer(config_.max_binary_frame_length_bytes);
  const ProbeResult<UnicoreBinaryFrame> probe =
      ProbeAtOffset<UnicoreBinaryFrameFramer, UnicoreBinaryFrame>(framer, buffer_, start_offset);

  if (probe.status == ParserStatus::kRecordReady && probe.record.has_value())
  {
    ++metrics_.binary_frames_seen;
    binary_seen_valid_frame_ = true;
    HandleBinaryFrame(*probe.record);
    next_offset = start_offset + probe.bytes_consumed;
    return true;
  }

  if (probe.status == ParserStatus::kTruncated)
  {
    if (finalizing)
    {
      ++metrics_.malformed_frames;
      next_offset = buffer_.size();
    }
    else
    {
      keep_tail = true;
      next_offset = start_offset;
    }
    return true;
  }

  if (probe.status == ParserStatus::kInvalidData || probe.status == ParserStatus::kOverflow)
  {
    if (!ShouldSuppressStartupBinaryMalformed())
    {
      ++metrics_.malformed_frames;
    }
  }

  next_offset = start_offset + 1u;
  return true;
}

bool UnicoreSession::ShouldSuppressStartupAsciiMalformed()
{
  if (ascii_seen_valid_record_ || ascii_startup_malformed_suppressed_)
  {
    return false;
  }

  ascii_startup_malformed_suppressed_ = true;
  return true;
}

bool UnicoreSession::ShouldSuppressStartupBinaryMalformed()
{
  if (binary_seen_valid_frame_ || binary_startup_malformed_suppressed_)
  {
    return false;
  }

  binary_startup_malformed_suppressed_ = true;
  return true;
}

void UnicoreSession::HandleFrame(const UnicoreFrame& frame)
{
  if (frame.message_name.empty())
  {
    ++metrics_.malformed_lines;
    return;
  }

  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    ++metrics_.malformed_lines;
    return;
  }

  if (!IsSupportedRecordName(frame.message_name))
  {
    ++metrics_.unknown_records;
    return;
  }

  if (frame.message_name == "PVTSLNA")
  {
    const auto parsed = universal_gnss_protocols::ParseUnicorePvtsln(frame);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.records_rejected;
      return;
    }

    ++metrics_.records_parsed;
    ++metrics_.runtime_observations;
    ++metrics_.position_observations;

    GnssRuntimeState update = universal_gnss_protocols::UnicorePvtslnToRuntimeState(*parsed.record);
    if (HasFreshMixedNmeaSample(seen_valid_nmea_gga_,
                                last_nmea_gga_timestamp_ns_,
                                update.timestamp_ns))
    {
      ClearOptionalValue(update, GnssCapability::kSatellitesUsed, update.satellites_used);
    }
    if (HasFreshMixedNmeaSample(seen_valid_nmea_gsv_,
                                last_nmea_gsv_timestamp_ns_,
                                update.timestamp_ns))
    {
      ClearOptionalValue(update, GnssCapability::kSatellitesTracked, update.satellites_tracked);
    }

    if (aggregator_.Merge(update))
    {
      ++metrics_.runtime_updates;
    }
    return;
  }

  if (frame.message_name == "BESTNAVA")
  {
    const auto parsed = universal_gnss_protocols::ParseUnicoreBestNav(frame);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.records_rejected;
      return;
    }

    ++metrics_.records_parsed;
    ++metrics_.runtime_observations;
    ++metrics_.position_observations;

    GnssRuntimeState update = universal_gnss_protocols::UnicoreBestNavToRuntimeState(*parsed.record);
    if (HasFreshMixedNmeaSample(seen_valid_nmea_gga_,
                                last_nmea_gga_timestamp_ns_,
                                update.timestamp_ns))
    {
      ClearOptionalValue(update, GnssCapability::kSatellitesUsed, update.satellites_used);
    }
    if (HasFreshMixedNmeaSample(seen_valid_nmea_gsv_,
                                last_nmea_gsv_timestamp_ns_,
                                update.timestamp_ns))
    {
      ClearOptionalValue(update, GnssCapability::kSatellitesTracked, update.satellites_tracked);
    }

    if (aggregator_.Merge(update))
    {
      ++metrics_.runtime_updates;
    }
    return;
  }

  if (frame.message_name == "RTKSTATUSA")
  {
    ParseAndMergeRecord(frame,
                        universal_gnss_protocols::ParseUnicoreRtkStatus,
                        universal_gnss_protocols::UnicoreRtkStatusToRuntimeState,
                        aggregator_,
                        metrics_);
    return;
  }

  if (frame.message_name == "RTCMSTATUSA")
  {
    ParseRtcmStatusRecord(frame, metrics_);
    return;
  }

  if (frame.message_name == "SATSINFOA")
  {
    ParseAndMergeRecord(frame,
                        universal_gnss_protocols::ParseUnicoreSatsInfo,
                        universal_gnss_protocols::UnicoreSatsInfoToRuntimeState,
                        aggregator_,
                        metrics_);
    return;
  }

  if (frame.message_name == "BESTSATA")
  {
    ParseAndMergeRecord(frame,
                        universal_gnss_protocols::ParseUnicoreBestSat,
                        universal_gnss_protocols::UnicoreBestSatToRuntimeState,
                        aggregator_,
                        metrics_);
    return;
  }

  if (frame.message_name == "JAMSTATUSA")
  {
    ParseAndMergeRecord(frame,
                        universal_gnss_protocols::ParseUnicoreJamStatus,
                        universal_gnss_protocols::UnicoreJamStatusToRuntimeState,
                        aggregator_,
                        metrics_);
    return;
  }

  if (frame.message_name == "FREQJAMSTATUSA")
  {
    ParseAndMergeRecord(frame,
                        universal_gnss_protocols::ParseUnicoreFreqJamStatus,
                        universal_gnss_protocols::UnicoreFreqJamStatusToRuntimeState,
                        aggregator_,
                        metrics_);
    return;
  }

  if (frame.message_name == "HWSTATUSA")
  {
    ParseRecordOnly(frame, universal_gnss_protocols::ParseUnicoreHwStatus, metrics_);
    return;
  }

  ParseRecordOnly(frame, universal_gnss_protocols::ParseUnicoreAgc, metrics_);
}

void UnicoreSession::HandleNmeaSentence(const NmeaSentence& sentence)
{
  if (sentence.checksum_status != ChecksumStatus::kValid)
  {
    ++metrics_.records_rejected;
    return;
  }

  if (!IsSupportedNmeaSentenceType(sentence))
  {
    ++metrics_.unknown_records;
    return;
  }

  if (universal_gnss_protocols::IsNmeaSentenceType(sentence, "GGA"))
  {
    const auto parsed = universal_gnss_protocols::ParseNmeaGga(sentence);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.records_rejected;
      return;
    }

    ++metrics_.records_parsed;
    ++metrics_.runtime_observations;
    ++metrics_.position_observations;
    seen_valid_nmea_gga_ = true;
    last_nmea_gga_timestamp_ns_ = sentence.timestamp_ns;

    GnssRuntimeState update = universal_gnss_protocols::NmeaGgaToRuntimeState(*parsed.record);
    PruneNmeaGgaFallback(aggregator_.state(),
                         HasFreshMixedNmeaSample(
                             seen_valid_nmea_gga_, last_nmea_gga_timestamp_ns_, sentence.timestamp_ns),
                         update);
    if (aggregator_.Merge(update))
    {
      ++metrics_.runtime_updates;
    }
    return;
  }

  if (universal_gnss_protocols::IsNmeaGst(sentence))
  {
    const auto parsed = universal_gnss_protocols::ParseNmeaGst(sentence);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.records_rejected;
      return;
    }

    ++metrics_.records_parsed;
    ++metrics_.runtime_observations;

    GnssRuntimeState update = universal_gnss_protocols::NmeaGstToRuntimeState(*parsed.record);
    PruneNmeaGstFallback(aggregator_.state(), update);
    if (aggregator_.Merge(update))
    {
      ++metrics_.runtime_updates;
    }
    return;
  }

  const auto parsed = universal_gnss_protocols::ParseNmeaGsv(sentence);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics_.records_rejected;
    return;
  }

  ++metrics_.records_parsed;
  ++metrics_.runtime_observations;
  seen_valid_nmea_gsv_ = true;
  last_nmea_gsv_timestamp_ns_ = sentence.timestamp_ns;

  const NmeaGsvRecord& record = *parsed.record;
  auto it = std::find_if(gsv_talker_states_.begin(),
                         gsv_talker_states_.end(),
                         [&](const UnicoreNmeaGsvTalkerState& state) {
                           return state.talker == sentence.talker;
                         });
  if (it == gsv_talker_states_.end())
  {
    it = gsv_talker_states_.emplace(gsv_talker_states_.end());
    it->talker = sentence.talker;
  }

  const bool stale_cycle = sentence.timestamp_ns.has_value() && it->last_timestamp_ns.has_value() &&
                           *sentence.timestamp_ns - *it->last_timestamp_ns >
                               kGsvTalkerFreshnessWindowNs;
  const bool invalid_message_count =
      record.total_messages == 0u ||
      record.total_messages > UnicoreNmeaGsvTalkerState::kMaxMessages;
  if (record.message_index <= 1u || stale_cycle || invalid_message_count ||
      it->total_messages != record.total_messages)
  {
    it->total_messages = invalid_message_count ? 1u : record.total_messages;
    it->satellites_in_view = record.satellites_in_view;
    it->tracked_satellites = 0u;
    it->last_timestamp_ns = sentence.timestamp_ns;
    it->seen_messages.fill(false);
    it->cn0_sum = 0.0f;
    it->cn0_count = 0u;
    it->cn0_max = 0.0f;
  }

  it->satellites_in_view = record.satellites_in_view;
  it->last_timestamp_ns = sentence.timestamp_ns;

  if (record.message_index >= 1u &&
      record.message_index <= UnicoreNmeaGsvTalkerState::kMaxMessages)
  {
    const std::size_t index = static_cast<std::size_t>(record.message_index - 1u);
    if (!it->seen_messages[index])
    {
      it->seen_messages[index] = true;
      const std::uint32_t tracked_next_total =
          static_cast<std::uint32_t>(it->tracked_satellites) + record.satellite_count;
      it->tracked_satellites =
          static_cast<std::uint16_t>(std::min<std::uint32_t>(
              tracked_next_total, std::numeric_limits<std::uint16_t>::max()));
      for (std::size_t satellite_index = 0u; satellite_index < record.satellite_count;
           ++satellite_index)
      {
        const auto& satellite = record.satellites[satellite_index];
        if (!satellite.cn0_db_hz.has_value())
        {
          continue;
        }

        it->cn0_sum += *satellite.cn0_db_hz;
        it->cn0_max =
            (it->cn0_count == 0u || *satellite.cn0_db_hz > it->cn0_max) ? *satellite.cn0_db_hz
                                                                         : it->cn0_max;
        ++it->cn0_count;
      }
    }
  }

  GnssRuntimeState update;
  update.timestamp_ns = sentence.timestamp_ns;
  SetCapability(update, GnssCapability::kSatellitesTracked);
  SetCapability(update, GnssCapability::kSatellitesVisible);
  SetCapability(update, GnssCapability::kMeanCn0);
  SetCapability(update, GnssCapability::kMaxCn0);

  std::uint16_t satellites_tracked_total = 0u;
  std::uint16_t satellites_visible_total = 0u;
  float cn0_sum_total = 0.0f;
  std::size_t cn0_count_total = 0u;
  float cn0_max_total = 0.0f;
  bool cn0_seen = false;

  for (const auto& talker_state : gsv_talker_states_)
  {
    const bool talker_is_fresh =
        !sentence.timestamp_ns.has_value() || !talker_state.last_timestamp_ns.has_value() ||
        (*sentence.timestamp_ns - *talker_state.last_timestamp_ns) <= kGsvTalkerFreshnessWindowNs;
    if (!talker_is_fresh)
    {
      continue;
    }

    const std::uint32_t tracked_next_total =
        static_cast<std::uint32_t>(satellites_tracked_total) + talker_state.tracked_satellites;
    satellites_tracked_total =
        static_cast<std::uint16_t>(std::min<std::uint32_t>(tracked_next_total,
                                                           std::numeric_limits<std::uint16_t>::max()));

    const std::uint32_t next_total =
        static_cast<std::uint32_t>(satellites_visible_total) + talker_state.satellites_in_view;
    satellites_visible_total =
        static_cast<std::uint16_t>(std::min<std::uint32_t>(next_total,
                                                           std::numeric_limits<std::uint16_t>::max()));

    cn0_sum_total += talker_state.cn0_sum;
    cn0_count_total += talker_state.cn0_count;
    if (talker_state.cn0_count > 0u)
    {
      cn0_max_total = cn0_seen ? std::max(cn0_max_total, talker_state.cn0_max) : talker_state.cn0_max;
      cn0_seen = true;
    }
  }

  if (aggregator_.state().satellites_used.has_value())
  {
    satellites_tracked_total = std::max(satellites_tracked_total, *aggregator_.state().satellites_used);
    satellites_visible_total = std::max(satellites_visible_total, satellites_tracked_total);
  }

  SetOptionalValue(update,
                   GnssCapability::kSatellitesTracked,
                   update.satellites_tracked,
                   satellites_tracked_total);
  SetOptionalValue(update,
                   GnssCapability::kSatellitesVisible,
                   update.satellites_visible,
                   satellites_visible_total);
  if (cn0_count_total > 0u)
  {
    SetOptionalValue(update,
                     GnssCapability::kMeanCn0,
                     update.mean_cn0_db_hz,
                     cn0_sum_total / static_cast<float>(cn0_count_total));
    SetOptionalValue(update, GnssCapability::kMaxCn0, update.max_cn0_db_hz, cn0_max_total);
  }

  if (aggregator_.Merge(update))
  {
    ++metrics_.runtime_updates;
  }
}

void UnicoreSession::HandleBinaryFrame(const UnicoreBinaryFrame& frame)
{
  if (!IsSupportedBinaryMessageId(frame.message_id))
  {
    ++metrics_.unknown_records;
    return;
  }

  if (frame.message_id == 2118u)
  {
    ParseAndMergeBinaryRecord(frame,
                              universal_gnss_protocols::ParseUnicoreBestNavB,
                              universal_gnss_protocols::UnicoreBestNavBToRuntimeState,
                              true,
                              aggregator_,
                              metrics_);
    return;
  }

  ParseAndMergeBinaryRecord(frame,
                            universal_gnss_protocols::ParseUnicorePvtslnB,
                            universal_gnss_protocols::UnicorePvtslnBToRuntimeState,
                            true,
                            aggregator_,
                            metrics_);
}

}  // namespace universal_gnss_driver
