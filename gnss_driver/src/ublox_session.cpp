#include "universal_gnss_driver/ublox_session.hpp"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/nmea_parser.hpp"
#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/ubx_parser.hpp"

namespace universal_gnss_driver
{

namespace
{

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::NmeaSentenceFramer;
using universal_gnss_protocols::ParserResult;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::RtcmFrame;
using universal_gnss_protocols::RtcmFrameFramer;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;

template <typename RecordT>
struct ProbeResult
{
  ParserStatus status{ParserStatus::kIdle};
  std::optional<RecordT> record{};
  std::size_t bytes_consumed{0u};
};

template <typename FramerT, typename RecordT>
ProbeResult<RecordT> ProbeAtOffset(FramerT& framer,
                                   const std::vector<UbloxSession::BufferedByte>& bytes,
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

bool IsSupportedUbxFrame(const UbxFrame& frame)
{
  return (frame.class_id == 0x01u && frame.message_id == 0x04u) ||
         (frame.class_id == 0x01u && frame.message_id == 0x07u) ||
         (frame.class_id == 0x01u && frame.message_id == 0x35u) ||
         (frame.class_id == 0x01u && frame.message_id == 0x03u) ||
         (frame.class_id == 0x02u && frame.message_id == 0x32u) ||
         (frame.class_id == 0x0Au && frame.message_id == 0x09u) ||
         (frame.class_id == 0x0Au && frame.message_id == 0x0Bu) ||
         (frame.class_id == 0x0Au && frame.message_id == 0x38u);
}

bool IsSupportedNmeaSentenceType(const NmeaSentence& sentence)
{
  return universal_gnss_protocols::IsNmeaSentenceType(sentence, "GGA") ||
         universal_gnss_protocols::IsNmeaSentenceType(sentence, "RMC") ||
         universal_gnss_protocols::IsNmeaSentenceType(sentence, "GSA") ||
         universal_gnss_protocols::IsNmeaSentenceType(sentence, "GSV") ||
         universal_gnss_protocols::IsNmeaGst(sentence);
}

template <typename FrameT, typename ParseFn, typename MapFn>
void ParseAndMergeFrame(const ParseFn& parse_fn,
                        const MapFn& map_fn,
                        const FrameT& frame,
                        const bool is_position_observation,
                        universal_gnss::GnssRuntimeAggregator& aggregator,
                        UbloxSessionMetrics& metrics)
{
  const auto parsed = parse_fn(frame);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics.frames_rejected;
    return;
  }

  ++metrics.frames_parsed;
  ++metrics.runtime_observations;
  if (is_position_observation)
  {
    ++metrics.position_observations;
  }
  if (aggregator.Merge(map_fn(*parsed.record)))
  {
    ++metrics.runtime_updates;
  }
}

}  // namespace

UbloxSession::UbloxSession(UbloxSessionConfig config) : config_(config)
{
}

void UbloxSession::FeedBytes(const std::uint8_t* data,
                             const std::size_t size,
                             const std::optional<std::int64_t> timestamp_ns)
{
  if (data == nullptr || size == 0u)
  {
    return;
  }

  metrics_.bytes_seen += size;
  buffer_.reserve(buffer_.size() + size);
  for (std::size_t index = 0u; index < size; ++index)
  {
    buffer_.push_back(BufferedByte{data[index], timestamp_ns});
  }

  ProcessBufferedData(false);
}

void UbloxSession::FeedBytes(const std::vector<std::uint8_t>& bytes,
                             const std::optional<std::int64_t> timestamp_ns)
{
  FeedBytes(bytes.data(), bytes.size(), timestamp_ns);
}

void UbloxSession::FeedString(const std::string_view text,
                              const std::optional<std::int64_t> timestamp_ns)
{
  FeedBytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), timestamp_ns);
}

void UbloxSession::Finalize()
{
  ProcessBufferedData(true);
}

void UbloxSession::Reset()
{
  buffer_.clear();
  aggregator_.Reset();
  metrics_ = UbloxSessionMetrics{};
}

const universal_gnss::GnssRuntimeState& UbloxSession::current_state() const
{
  return aggregator_.state();
}

const UbloxSessionMetrics& UbloxSession::metrics() const
{
  return metrics_;
}

const UbloxSessionConfig& UbloxSession::config() const
{
  return config_;
}

void UbloxSession::ProcessBufferedData(const bool finalizing)
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
    else if (byte == 0xB5u)
    {
      consumed = ConsumeUbxAtOffset(offset, finalizing, next_offset, keep_tail);
    }
    else if (byte == 0xD3u)
    {
      consumed = ConsumeRtcmAtOffset(offset, finalizing, next_offset, keep_tail);
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

bool UbloxSession::ConsumeNmeaAtOffset(const std::size_t start_offset,
                                       const bool finalizing,
                                       std::size_t& next_offset,
                                       bool& keep_tail)
{
  NmeaSentenceFramer framer(config_.max_nmea_sentence_length_bytes);
  const ProbeResult<NmeaSentence> probe =
      ProbeAtOffset<NmeaSentenceFramer, NmeaSentence>(framer, buffer_, start_offset);

  if (probe.status == ParserStatus::kRecordReady && probe.record.has_value())
  {
    ++metrics_.nmea_sentences_seen;
    if (probe.record->checksum_status != ChecksumStatus::kValid)
    {
      ++metrics_.malformed_frames;
    }
    else
    {
      RouteNmeaSentence(*probe.record);
    }
    next_offset = start_offset + probe.record->raw_bytes.size();
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
    ++metrics_.malformed_frames;
  }

  next_offset = start_offset + 1u;
  return true;
}

bool UbloxSession::ConsumeUbxAtOffset(const std::size_t start_offset,
                                      const bool finalizing,
                                      std::size_t& next_offset,
                                      bool& keep_tail)
{
  UbxFrameFramer framer(config_.max_ubx_frame_length_bytes);
  const ProbeResult<UbxFrame> probe =
      ProbeAtOffset<UbxFrameFramer, UbxFrame>(framer, buffer_, start_offset);

  if (probe.status == ParserStatus::kRecordReady && probe.record.has_value())
  {
    ++metrics_.ubx_frames_seen;
    if (probe.record->checksum_status != ChecksumStatus::kValid)
    {
      ++metrics_.malformed_frames;
    }
    else
    {
      RouteUbxFrame(*probe.record);
    }
    next_offset = start_offset + probe.record->raw_bytes.size();
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
    ++metrics_.malformed_frames;
  }

  next_offset = start_offset + 1u;
  return true;
}

bool UbloxSession::ConsumeRtcmAtOffset(const std::size_t start_offset,
                                       const bool finalizing,
                                       std::size_t& next_offset,
                                       bool& keep_tail)
{
  RtcmFrameFramer framer(config_.max_rtcm_frame_length_bytes);
  const ProbeResult<RtcmFrame> probe =
      ProbeAtOffset<RtcmFrameFramer, RtcmFrame>(framer, buffer_, start_offset);

  if (probe.status == ParserStatus::kRecordReady && probe.record.has_value())
  {
    ++metrics_.rtcm_frames_seen;
    if (probe.record->checksum_status != ChecksumStatus::kValid)
    {
      ++metrics_.malformed_frames;
    }
    else
    {
      RouteRtcmFrame(*probe.record);
    }
    next_offset = start_offset + probe.record->raw_bytes.size();
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
    ++metrics_.malformed_frames;
  }

  next_offset = start_offset + 1u;
  return true;
}

void UbloxSession::RouteNmeaSentence(const NmeaSentence& sentence)
{
  if (!IsSupportedNmeaSentenceType(sentence))
  {
    ++metrics_.unknown_frames;
    return;
  }

  if (universal_gnss_protocols::IsNmeaSentenceType(sentence, "GGA"))
  {
    const auto parsed = universal_gnss_protocols::ParseNmeaGga(sentence);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.frames_rejected;
      return;
    }

    ++metrics_.frames_parsed;
    ++metrics_.runtime_observations;
    if (config_.enable_nmea_runtime_updates)
    {
      ++metrics_.position_observations;
    }
    if (config_.enable_nmea_runtime_updates &&
        aggregator_.Merge(universal_gnss_protocols::NmeaGgaToRuntimeState(*parsed.record)))
    {
      ++metrics_.runtime_updates;
    }
    return;
  }

  if (universal_gnss_protocols::IsNmeaSentenceType(sentence, "RMC"))
  {
    const auto parsed = universal_gnss_protocols::ParseNmeaRmc(sentence);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.frames_rejected;
      return;
    }

    ++metrics_.frames_parsed;
    ++metrics_.runtime_observations;
    if (config_.enable_nmea_runtime_updates)
    {
      ++metrics_.position_observations;
    }
    if (config_.enable_nmea_runtime_updates &&
        aggregator_.Merge(universal_gnss_protocols::NmeaRmcToRuntimeState(*parsed.record)))
    {
      ++metrics_.runtime_updates;
    }
    return;
  }

  if (universal_gnss_protocols::IsNmeaSentenceType(sentence, "GSA"))
  {
    const auto parsed = universal_gnss_protocols::ParseNmeaGsa(sentence);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.frames_rejected;
      return;
    }

    ++metrics_.frames_parsed;
    ++metrics_.runtime_observations;
    if (config_.enable_nmea_runtime_updates &&
        aggregator_.Merge(universal_gnss_protocols::NmeaGsaToRuntimeState(*parsed.record)))
    {
      ++metrics_.runtime_updates;
    }
    return;
  }

  if (universal_gnss_protocols::IsNmeaSentenceType(sentence, "GSV"))
  {
    const auto parsed = universal_gnss_protocols::ParseNmeaGsv(sentence);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.frames_rejected;
      return;
    }

    ++metrics_.frames_parsed;
    ++metrics_.runtime_observations;
    if (config_.enable_nmea_runtime_updates)
    {
      universal_gnss::GnssRuntimeState update;
      universal_gnss_protocols::MergeNmeaGsvIntoRuntimeState(*parsed.record, update);
      if (aggregator_.Merge(update))
      {
        ++metrics_.runtime_updates;
      }
    }
    return;
  }

  const auto parsed = universal_gnss_protocols::ParseNmeaGst(sentence);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics_.frames_rejected;
    return;
  }

  ++metrics_.frames_parsed;
  ++metrics_.runtime_observations;
  if (config_.enable_nmea_runtime_updates &&
      aggregator_.Merge(universal_gnss_protocols::NmeaGstToRuntimeState(*parsed.record)))
  {
    ++metrics_.runtime_updates;
  }
}

void UbloxSession::RouteUbxFrame(const UbxFrame& frame)
{
  if (!IsSupportedUbxFrame(frame))
  {
    ++metrics_.unknown_frames;
    return;
  }

  if (frame.class_id == 0x01u && frame.message_id == 0x07u)
  {
    ParseAndMergeFrame(universal_gnss_protocols::ParseUbxNavPvt,
                       universal_gnss_protocols::UbxNavPvtToRuntimeState,
                       frame,
                       true,
                       aggregator_,
                       metrics_);
    return;
  }

  if (frame.class_id == 0x01u && frame.message_id == 0x35u)
  {
    ParseAndMergeFrame(universal_gnss_protocols::ParseUbxNavSat,
                       universal_gnss_protocols::UbxNavSatToRuntimeState,
                       frame,
                       false,
                       aggregator_,
                       metrics_);
    return;
  }

  if (frame.class_id == 0x01u && frame.message_id == 0x03u)
  {
    ParseAndMergeFrame(universal_gnss_protocols::ParseUbxNavStatus,
                       universal_gnss_protocols::UbxNavStatusToRuntimeState,
                       frame,
                       false,
                       aggregator_,
                       metrics_);
    return;
  }

  if (frame.class_id == 0x01u && frame.message_id == 0x04u)
  {
    ParseAndMergeFrame(universal_gnss_protocols::ParseUbxNavDop,
                       universal_gnss_protocols::UbxNavDopToRuntimeState,
                       frame,
                       false,
                       aggregator_,
                       metrics_);
    return;
  }

  if (frame.class_id == 0x0Au && frame.message_id == 0x09u)
  {
    ParseAndMergeFrame(universal_gnss_protocols::ParseUbxMonHw,
                       universal_gnss_protocols::UbxMonHwToRuntimeState,
                       frame,
                       false,
                       aggregator_,
                       metrics_);
    return;
  }

  if (frame.class_id == 0x0Au && frame.message_id == 0x0Bu)
  {
    const auto parsed = universal_gnss_protocols::ParseUbxMonHw2(frame);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.frames_rejected;
      return;
    }

    ++metrics_.frames_parsed;
    return;
  }

  if (frame.class_id == 0x02u && frame.message_id == 0x32u)
  {
    const auto parsed = universal_gnss_protocols::ParseUbxRxmRtcm(frame);
    if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
    {
      ++metrics_.frames_rejected;
      return;
    }

    ++metrics_.frames_parsed;
    ++metrics_.receiver_rtcm_messages_seen;
    metrics_.last_receiver_rtcm_message_type = parsed.record->message_type;
    if (parsed.record->crc_failed)
    {
      ++metrics_.receiver_rtcm_crc_failed;
      return;
    }

    switch (parsed.record->message_use)
    {
      case universal_gnss_protocols::UbxRxmRtcmMessageUse::kUsed:
        ++metrics_.receiver_rtcm_messages_used;
        break;

      case universal_gnss_protocols::UbxRxmRtcmMessageUse::kNotUsed:
        ++metrics_.receiver_rtcm_messages_not_used;
        break;

      case universal_gnss_protocols::UbxRxmRtcmMessageUse::kUnknown:
      default:
        break;
    }
    return;
  }

  ParseAndMergeFrame(universal_gnss_protocols::ParseUbxMonRf,
                     universal_gnss_protocols::UbxMonRfToRuntimeState,
                     frame,
                     false,
                     aggregator_,
                     metrics_);
}

void UbloxSession::RouteRtcmFrame(const RtcmFrame& frame)
{
  const auto parsed = universal_gnss_protocols::ParseRtcmMessageInfo(frame);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics_.frames_rejected;
    return;
  }

  ++metrics_.frames_parsed;
  ++metrics_.rtcm_message_type_counts[parsed.record->message_type];
}

}  // namespace universal_gnss_driver
