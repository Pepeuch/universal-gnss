#include "universal_gnss_driver/receiver_session.hpp"

#include <optional>
#include <utility>
#include <vector>

#include "universal_gnss_driver/stream_detector.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/unicore_framer.hpp"

namespace universal_gnss_driver
{

namespace
{

using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_protocols::UnicoreBinaryFrame;
using universal_gnss_protocols::UnicoreBinaryFrameFramer;
using universal_gnss_protocols::UnicoreFrame;
using universal_gnss_protocols::UnicoreFrameFramer;

struct VendorDetectionCandidate
{
  ReceiverSessionKind kind{ReceiverSessionKind::kAutoDetect};
  std::size_t bytes_consumed{0u};
};

template <typename RecordT>
struct ProbeResult
{
  ParserStatus status{ParserStatus::kIdle};
  std::optional<RecordT> record{};
  std::size_t bytes_consumed{0u};
};

template <typename FramerT, typename RecordT, typename AcceptFn>
std::optional<VendorDetectionCandidate>
DetectVendorCandidate(FramerT& framer,
                      const detail::TimestampedByteBuffer& bytes,
                      const ReceiverSessionKind kind,
                      AcceptFn&& accept)
{
  framer.Reset();
  std::size_t consumed = 0;
  for (auto input = bytes.cursor(); input.valid(); input.next())
  {
    ++consumed;
    const auto result = framer.PushByte(input.value(), input.timestamp());
    if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
    {
      continue;
    }

    if (!accept(*result.record))
    {
      continue;
    }

    return VendorDetectionCandidate{kind, consumed};
  }

  return std::nullopt;
}

bool IsSupportedUnicoreCandidate(const UnicoreFrame& frame)
{
  return frame.sync_char != '$' && !frame.message_name.empty();
}

bool IsSupportedUnicoreBinaryCandidate(const UnicoreBinaryFrame& frame)
{
  return frame.checksum_status == universal_gnss_protocols::ChecksumStatus::kValid;
}

}  // namespace

ReceiverSession::ReceiverSession(ReceiverSessionConfig config)
    : config_(config), nmea_session_(config.nmea), ublox_session_(config.ublox),
      unicore_session_(config.unicore)
{
  InitializeSelectionFromConfig();
}

void ReceiverSession::FeedBytes(const std::uint8_t* data,
                                const std::size_t size,
                                const std::optional<std::int64_t> timestamp_ns)
{
  if (data == nullptr || size == 0u)
  {
    return;
  }

  metrics_.bytes_seen += size;

  if (metrics_.selected_session_kind.has_value())
  {
    RouteToSelectedSession(data, size, timestamp_ns);
    RefreshMetricsFromSelectedSession();
    return;
  }

  AppendPendingBytes(data, size, timestamp_ns);
  TrySelectSessionFromPendingBytes();
  if (metrics_.selected_session_kind.has_value())
  {
    RouteBufferedBytesToSelectedSession();
  }
  else
  {
    TrimPendingBytesIfNeeded();
  }
  RefreshMetricsFromSelectedSession();
}

void ReceiverSession::FeedBytes(const std::vector<std::uint8_t>& bytes,
                                const std::optional<std::int64_t> timestamp_ns)
{
  FeedBytes(bytes.data(), bytes.size(), timestamp_ns);
}

void ReceiverSession::FeedString(const std::string_view text,
                                 const std::optional<std::int64_t> timestamp_ns)
{
  FeedBytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), timestamp_ns);
}

void ReceiverSession::Finalize()
{
  if (!metrics_.selected_session_kind.has_value())
  {
    TrySelectSessionFromPendingBytes();
    if (metrics_.selected_session_kind.has_value())
    {
      RouteBufferedBytesToSelectedSession();
    }
  }

  if (metrics_.selected_session_kind == ReceiverSessionKind::kUblox)
  {
    ublox_session_.Finalize();
  }
  else if (metrics_.selected_session_kind == ReceiverSessionKind::kUnicore)
  {
    unicore_session_.Finalize();
  }
  else if (metrics_.selected_session_kind == ReceiverSessionKind::kNmea)
  {
    nmea_session_.Finalize();
  }
  else
  {
    pending_auto_detect_bytes_.clear();
    pending_auto_detect_bytes_.ConstrainCapacity(config_.max_auto_detect_buffer_bytes);
  }

  RefreshMetricsFromSelectedSession();
}

void ReceiverSession::Reset()
{
  nmea_session_.Reset();
  ublox_session_.Reset();
  unicore_session_.Reset();
  pending_auto_detect_bytes_.clear();
  pending_auto_detect_bytes_.ConstrainCapacity(config_.max_auto_detect_buffer_bytes);
  metrics_ = ReceiverSessionMetrics{};
  InitializeSelectionFromConfig();
}

const universal_gnss::GnssRuntimeState& ReceiverSession::current_state() const
{
  if (metrics_.selected_session_kind == ReceiverSessionKind::kUblox)
  {
    return ublox_session_.current_state();
  }
  if (metrics_.selected_session_kind == ReceiverSessionKind::kUnicore)
  {
    return unicore_session_.current_state();
  }
  if (metrics_.selected_session_kind == ReceiverSessionKind::kNmea)
  {
    return nmea_session_.current_state();
  }
  return empty_state_;
}

const ReceiverSessionMetrics& ReceiverSession::metrics() const
{
  return metrics_;
}

const ReceiverSessionConfig& ReceiverSession::config() const
{
  return config_;
}

const UbloxSessionMetrics& ReceiverSession::ublox_metrics() const
{
  return ublox_session_.metrics();
}

const UnicoreSessionMetrics& ReceiverSession::unicore_metrics() const
{
  return unicore_session_.metrics();
}

const NmeaSessionMetrics& ReceiverSession::nmea_metrics() const
{
  return nmea_session_.metrics();
}

void ReceiverSession::InitializeSelectionFromConfig()
{
  if (config_.kind == ReceiverSessionKind::kUblox ||
      config_.kind == ReceiverSessionKind::kUnicore || config_.kind == ReceiverSessionKind::kNmea)
  {
    metrics_.selected_session_kind = config_.kind;
    metrics_.selection_locked = true;
  }
  else
  {
    metrics_.selected_session_kind.reset();
    metrics_.selection_locked = false;
  }
}

void ReceiverSession::RouteToSelectedSession(const std::uint8_t* data,
                                             const std::size_t size,
                                             const std::optional<std::int64_t> timestamp_ns)
{
  if (metrics_.selected_session_kind == ReceiverSessionKind::kUblox)
  {
    ublox_session_.FeedBytes(data, size, timestamp_ns);
    return;
  }

  if (metrics_.selected_session_kind == ReceiverSessionKind::kUnicore)
  {
    unicore_session_.FeedBytes(data, size, timestamp_ns);
    return;
  }

  if (metrics_.selected_session_kind == ReceiverSessionKind::kNmea)
  {
    nmea_session_.FeedBytes(data, size, timestamp_ns);
  }
}

void ReceiverSession::RouteBufferedBytesToSelectedSession()
{
  if (!metrics_.selected_session_kind.has_value())
  {
    return;
  }

  for (auto input = pending_auto_detect_bytes_.cursor(); input.valid(); input.next())
  {
    const auto value = input.value();
    RouteToSelectedSession(&value, 1u, input.timestamp());
  }
  pending_auto_detect_bytes_.clear();
  pending_auto_detect_bytes_.ConstrainCapacity(config_.max_auto_detect_buffer_bytes);
}

void ReceiverSession::AppendPendingBytes(const std::uint8_t* data,
                                         const std::size_t size,
                                         const std::optional<std::int64_t> timestamp_ns)
{
  pending_auto_detect_bytes_.Append(data, size, timestamp_ns);
}

void ReceiverSession::TrimPendingBytesIfNeeded()
{
  if (pending_auto_detect_bytes_.size() > config_.max_auto_detect_buffer_bytes)
    pending_auto_detect_bytes_.ErasePrefix(pending_auto_detect_bytes_.size() -
                                           config_.max_auto_detect_buffer_bytes);
  pending_auto_detect_bytes_.ConstrainCapacity(config_.max_auto_detect_buffer_bytes);
}

void ReceiverSession::TrySelectSessionFromPendingBytes()
{
  if (metrics_.selected_session_kind.has_value() || pending_auto_detect_bytes_.empty())
  {
    return;
  }

  const StreamDetector detector;
  const StreamDetectionResult earliest_detection =
      detector.Detect(pending_auto_detect_bytes_.data(), pending_auto_detect_bytes_.size());
  if (earliest_detection.protocol == DetectedStreamProtocol::kUbx)
  {
    SelectSession(ReceiverSessionKind::kUblox);
    return;
  }
  if (earliest_detection.protocol == DetectedStreamProtocol::kUnicoreAscii)
  {
    SelectSession(ReceiverSessionKind::kUnicore);
    return;
  }
  if (earliest_detection.protocol == DetectedStreamProtocol::kUnicoreBinary)
  {
    SelectSession(ReceiverSessionKind::kUnicore);
    return;
  }

  UbxFrameFramer ubx_framer(config_.ublox.max_ubx_frame_length_bytes);
  const auto ubx_candidate = DetectVendorCandidate<UbxFrameFramer, UbxFrame>(
      ubx_framer,
      pending_auto_detect_bytes_,
      ReceiverSessionKind::kUblox,
      [](const UbxFrame& frame) {
        return frame.checksum_status == universal_gnss_protocols::ChecksumStatus::kValid;
      });

  UnicoreFrameFramer unicore_framer(config_.unicore.max_frame_length_bytes);
  const auto unicore_candidate =
      DetectVendorCandidate<UnicoreFrameFramer, UnicoreFrame>(unicore_framer,
                                                              pending_auto_detect_bytes_,
                                                              ReceiverSessionKind::kUnicore,
                                                              IsSupportedUnicoreCandidate);

  UnicoreBinaryFrameFramer unicore_binary_framer(config_.unicore.max_binary_frame_length_bytes);
  const auto unicore_binary_candidate =
      DetectVendorCandidate<UnicoreBinaryFrameFramer, UnicoreBinaryFrame>(
          unicore_binary_framer,
          pending_auto_detect_bytes_,
          ReceiverSessionKind::kUnicore,
          IsSupportedUnicoreBinaryCandidate);

  const bool has_unicore_candidate =
      unicore_candidate.has_value() || unicore_binary_candidate.has_value();
  if (ubx_candidate.has_value() == has_unicore_candidate)
  {
    if (config_.allow_generic_nmea_auto_detect &&
        earliest_detection.protocol == DetectedStreamProtocol::kNmea &&
        !ubx_candidate.has_value() && !has_unicore_candidate)
    {
      SelectSession(ReceiverSessionKind::kNmea);
    }
    return;
  }

  if (ubx_candidate.has_value())
  {
    SelectSession(ReceiverSessionKind::kUblox);
    return;
  }

  if (has_unicore_candidate)
  {
    SelectSession(ReceiverSessionKind::kUnicore);
    return;
  }

  if (config_.allow_generic_nmea_auto_detect &&
      earliest_detection.protocol == DetectedStreamProtocol::kNmea)
  {
    SelectSession(ReceiverSessionKind::kNmea);
  }
}

void ReceiverSession::SelectSession(const ReceiverSessionKind kind)
{
  if (metrics_.selected_session_kind.has_value())
  {
    if (*metrics_.selected_session_kind != kind)
    {
      ++metrics_.sessions_switched;
    }
    return;
  }

  metrics_.selected_session_kind = kind;
  metrics_.selection_locked = true;
}

void ReceiverSession::RefreshMetricsFromSelectedSession()
{
  if (metrics_.selected_session_kind == ReceiverSessionKind::kUblox)
  {
    const auto& child = ublox_session_.metrics();
    metrics_.runtime_observations = child.runtime_observations;
    metrics_.position_observations = child.position_observations;
    metrics_.runtime_updates = child.runtime_updates;
    metrics_.malformed_records = child.malformed_frames;
    metrics_.rejected_records = child.frames_rejected;
    metrics_.parser_anomalies = metrics_.malformed_records + metrics_.rejected_records;
    metrics_.unknown_records = child.unknown_frames;
    metrics_.position_payload_freshness = child.position_payload_freshness;
    return;
  }

  if (metrics_.selected_session_kind == ReceiverSessionKind::kUnicore)
  {
    const auto& child = unicore_session_.metrics();
    metrics_.runtime_observations = child.runtime_observations;
    metrics_.position_observations = child.position_observations;
    metrics_.runtime_updates = child.runtime_updates;
    metrics_.malformed_records = child.malformed_lines + child.malformed_frames;
    metrics_.rejected_records = child.records_rejected;
    metrics_.parser_anomalies = metrics_.malformed_records + metrics_.rejected_records;
    metrics_.unknown_records = child.unknown_records;
    metrics_.position_payload_freshness = child.position_payload_freshness;
    return;
  }

  if (metrics_.selected_session_kind == ReceiverSessionKind::kNmea)
  {
    const auto& child = nmea_session_.metrics();
    metrics_.runtime_observations = child.runtime_observations;
    metrics_.position_observations = child.position_observations;
    metrics_.runtime_updates = child.runtime_updates;
    metrics_.malformed_records = child.malformed_sentences;
    metrics_.rejected_records = child.records_rejected;
    metrics_.parser_anomalies = metrics_.malformed_records + metrics_.rejected_records;
    metrics_.unknown_records = child.unknown_sentences;
    metrics_.position_payload_freshness = child.position_payload_freshness;
    return;
  }

  metrics_.runtime_observations = 0u;
  metrics_.position_observations = 0u;
  metrics_.runtime_updates = 0u;
  metrics_.malformed_records = 0u;
  metrics_.rejected_records = 0u;
  metrics_.parser_anomalies = 0u;
  metrics_.unknown_records = 0u;
  metrics_.position_payload_freshness = PositionPayloadFreshnessMetrics{};
}

const char* ToString(const ReceiverSessionKind kind)
{
  switch (kind)
  {
    case ReceiverSessionKind::kAutoDetect:
      return "auto_detect";
    case ReceiverSessionKind::kUblox:
      return "ublox";
    case ReceiverSessionKind::kUnicore:
      return "unicore";
    case ReceiverSessionKind::kNmea:
      return "nmea";
    default:
      return "unknown";
  }
}

}  // namespace universal_gnss_driver
