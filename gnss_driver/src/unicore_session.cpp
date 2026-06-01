#include "universal_gnss_driver/unicore_session.hpp"

#include <string_view>
#include <utility>

#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/unicore_parser.hpp"

namespace universal_gnss_driver
{

namespace
{

using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UnicoreBinaryFrame;
using universal_gnss_protocols::UnicoreFrame;

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
  if (aggregator.Merge(std::forward<MapFn>(map_fn)(*parsed.record)))
  {
    ++metrics.runtime_updates;
    return true;
  }
  return false;
}

template <typename ParseFn>
void ParseRecordOnly(const UnicoreFrame& frame,
                     ParseFn&& parse_fn,
                     UnicoreSessionMetrics& metrics)
{
  const auto parsed = std::forward<ParseFn>(parse_fn)(frame);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    ++metrics.records_rejected;
    return;
  }

  ++metrics.records_parsed;
}

bool IsSupportedRecordName(const std::string_view name)
{
  return name == "PVTSLNA" || name == "BESTNAVA" || name == "RTKSTATUSA" ||
         name == "RTCMSTATUSA" || name == "BESTSATA" || name == "SATSINFOA" ||
         name == "JAMSTATUSA" || name == "FREQJAMSTATUSA" || name == "HWSTATUSA" ||
         name == "AGCA";
}

bool IsSupportedBinaryMessageId(const std::uint16_t message_id)
{
  return message_id == 2118u || message_id == 1021u;
}

}  // namespace

UnicoreSession::UnicoreSession(UnicoreSessionConfig config)
    : config_(config),
      framer_(config.max_frame_length_bytes),
      binary_framer_(config.max_binary_frame_length_bytes)
{
}

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
    HandleFramerResult(framer_.PushByte(data[i], timestamp_ns));
    HandleBinaryFramerResult(binary_framer_.PushByte(data[i], timestamp_ns));
  }
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
  HandleFramerResult(framer_.Finalize());
  HandleBinaryFramerResult(binary_framer_.Finalize());
}

void UnicoreSession::Reset()
{
  framer_.Reset();
  binary_framer_.Reset();
  aggregator_.Reset();
  metrics_ = UnicoreSessionMetrics{};
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

void UnicoreSession::HandleFramerResult(
    const universal_gnss_protocols::ParserResult<universal_gnss_protocols::UnicoreFrame>& result)
{
  switch (result.status)
  {
    case ParserStatus::kRecordReady:
      ++metrics_.lines_seen;
      if (!result.record.has_value())
      {
        ++metrics_.malformed_lines;
        return;
      }
      if (!result.record->message_name.empty())
      {
        ++metrics_.ascii_records_seen;
      }
      HandleFrame(*result.record);
      return;

    case ParserStatus::kOverflow:
    case ParserStatus::kTruncated:
      ++metrics_.malformed_lines;
      return;

    case ParserStatus::kIdle:
    case ParserStatus::kNeedMoreData:
    case ParserStatus::kSkipped:
    case ParserStatus::kInvalidData:
      return;
  }
}

void UnicoreSession::HandleBinaryFramerResult(
    const universal_gnss_protocols::ParserResult<universal_gnss_protocols::UnicoreBinaryFrame>&
        result)
{
  switch (result.status)
  {
    case ParserStatus::kRecordReady:
      ++metrics_.binary_frames_seen;
      if (!result.record.has_value())
      {
        ++metrics_.malformed_frames;
        return;
      }
      HandleBinaryFrame(*result.record);
      return;

    case ParserStatus::kOverflow:
    case ParserStatus::kTruncated:
    case ParserStatus::kInvalidData:
      ++metrics_.malformed_frames;
      return;

    case ParserStatus::kIdle:
    case ParserStatus::kNeedMoreData:
    case ParserStatus::kSkipped:
      return;
  }
}

void UnicoreSession::HandleFrame(const UnicoreFrame& frame)
{
  if (frame.message_name.empty())
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
    ParseAndMergeRecord(
        frame,
        universal_gnss_protocols::ParseUnicorePvtsln,
        universal_gnss_protocols::UnicorePvtslnToRuntimeState,
        aggregator_,
        metrics_);
    return;
  }

  if (frame.message_name == "BESTNAVA")
  {
    ParseAndMergeRecord(
        frame,
        universal_gnss_protocols::ParseUnicoreBestNav,
        universal_gnss_protocols::UnicoreBestNavToRuntimeState,
        aggregator_,
        metrics_);
    return;
  }

  if (frame.message_name == "RTKSTATUSA")
  {
    ParseAndMergeRecord(
        frame,
        universal_gnss_protocols::ParseUnicoreRtkStatus,
        universal_gnss_protocols::UnicoreRtkStatusToRuntimeState,
        aggregator_,
        metrics_);
    return;
  }

  if (frame.message_name == "RTCMSTATUSA")
  {
    ParseAndMergeRecord(
        frame,
        universal_gnss_protocols::ParseUnicoreRtcmStatus,
        universal_gnss_protocols::UnicoreRtcmStatusToRuntimeState,
        aggregator_,
        metrics_);
    return;
  }

  if (frame.message_name == "SATSINFOA")
  {
    ParseAndMergeRecord(
        frame,
        universal_gnss_protocols::ParseUnicoreSatsInfo,
        universal_gnss_protocols::UnicoreSatsInfoToRuntimeState,
        aggregator_,
        metrics_);
    return;
  }

  if (frame.message_name == "BESTSATA")
  {
    ParseAndMergeRecord(
        frame,
        universal_gnss_protocols::ParseUnicoreBestSat,
        universal_gnss_protocols::UnicoreBestSatToRuntimeState,
        aggregator_,
        metrics_);
    return;
  }

  if (frame.message_name == "JAMSTATUSA")
  {
    ParseAndMergeRecord(
        frame,
        universal_gnss_protocols::ParseUnicoreJamStatus,
        universal_gnss_protocols::UnicoreJamStatusToRuntimeState,
        aggregator_,
        metrics_);
    return;
  }

  if (frame.message_name == "FREQJAMSTATUSA")
  {
    ParseAndMergeRecord(
        frame,
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

void UnicoreSession::HandleBinaryFrame(const UnicoreBinaryFrame& frame)
{
  if (!IsSupportedBinaryMessageId(frame.message_id))
  {
    ++metrics_.unknown_records;
    return;
  }

  if (frame.message_id == 2118u)
  {
    ParseAndMergeBinaryRecord(
        frame,
        universal_gnss_protocols::ParseUnicoreBestNavB,
        universal_gnss_protocols::UnicoreBestNavBToRuntimeState,
        aggregator_,
        metrics_);
    return;
  }

  ParseAndMergeBinaryRecord(
      frame,
      universal_gnss_protocols::ParseUnicorePvtslnB,
      universal_gnss_protocols::UnicorePvtslnBToRuntimeState,
      aggregator_,
      metrics_);
}

}  // namespace universal_gnss_driver
