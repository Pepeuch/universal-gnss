#include "universal_gnss_driver/receiver_session.hpp"

#include <optional>
#include <utility>
#include <vector>

#include "universal_gnss_driver/stream_detector.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/unicore_framer.hpp"

namespace universal_gnss_driver
{

namespace
{

using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::UnicoreBinaryFrame;
using universal_gnss_protocols::UnicoreBinaryFrameFramer;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;
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
std::optional<VendorDetectionCandidate> DetectVendorCandidate(
    FramerT& framer,
    const std::vector<ReceiverSession::BufferedByte>& bytes,
    const ReceiverSessionKind kind,
    AcceptFn&& accept)
{
  framer.Reset();
  for (std::size_t index = 0u; index < bytes.size(); ++index)
  {
    const auto result = framer.PushByte(bytes[index].value, bytes[index].timestamp_ns);
    if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
    {
      continue;
    }

    if (!accept(*result.record))
    {
      continue;
    }

    return VendorDetectionCandidate{kind, index + 1u};
  }

  return std::nullopt;
}

std::vector<std::uint8_t> ToByteVector(const std::vector<ReceiverSession::BufferedByte>& bytes)
{
  std::vector<std::uint8_t> values;
  values.reserve(bytes.size());
  for (const auto& byte : bytes)
  {
    values.push_back(byte.value);
  }
  return values;
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
    : config_(config), ublox_session_(config.ublox), unicore_session_(config.unicore)
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
  else
  {
    pending_auto_detect_bytes_.clear();
  }

  RefreshMetricsFromSelectedSession();
}

void ReceiverSession::Reset()
{
  ublox_session_.Reset();
  unicore_session_.Reset();
  pending_auto_detect_bytes_.clear();
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

void ReceiverSession::InitializeSelectionFromConfig()
{
  if (config_.kind == ReceiverSessionKind::kUblox ||
      config_.kind == ReceiverSessionKind::kUnicore)
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
  }
}

void ReceiverSession::RouteBufferedBytesToSelectedSession()
{
  if (!metrics_.selected_session_kind.has_value())
  {
    return;
  }

  for (const auto& byte : pending_auto_detect_bytes_)
  {
    RouteToSelectedSession(&byte.value, 1u, byte.timestamp_ns);
  }
  pending_auto_detect_bytes_.clear();
}

void ReceiverSession::AppendPendingBytes(const std::uint8_t* data,
                                         const std::size_t size,
                                         const std::optional<std::int64_t> timestamp_ns)
{
  pending_auto_detect_bytes_.reserve(pending_auto_detect_bytes_.size() + size);
  for (std::size_t index = 0u; index < size; ++index)
  {
    pending_auto_detect_bytes_.push_back(BufferedByte{data[index], timestamp_ns});
  }
}

void ReceiverSession::TrimPendingBytesIfNeeded()
{
  if (pending_auto_detect_bytes_.size() <= config_.max_auto_detect_buffer_bytes)
  {
    return;
  }

  const std::size_t bytes_to_drop =
      pending_auto_detect_bytes_.size() - config_.max_auto_detect_buffer_bytes;
  pending_auto_detect_bytes_.erase(
      pending_auto_detect_bytes_.begin(),
      pending_auto_detect_bytes_.begin() + static_cast<std::ptrdiff_t>(bytes_to_drop));
}

void ReceiverSession::TrySelectSessionFromPendingBytes()
{
  if (metrics_.selected_session_kind.has_value() || pending_auto_detect_bytes_.empty())
  {
    return;
  }

  const StreamDetector detector;
  const auto byte_values = ToByteVector(pending_auto_detect_bytes_);
  const StreamDetectionResult earliest_detection = detector.Detect(byte_values);
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
  const auto unicore_candidate = DetectVendorCandidate<UnicoreFrameFramer, UnicoreFrame>(
      unicore_framer,
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
    return;
  }

  if (ubx_candidate.has_value())
  {
    SelectSession(ReceiverSessionKind::kUblox);
    return;
  }

  SelectSession(ReceiverSessionKind::kUnicore);
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
    metrics_.runtime_updates = child.runtime_updates;
    metrics_.malformed_records = child.malformed_frames + child.frames_rejected;
    metrics_.unknown_records = child.unknown_frames;
    return;
  }

  if (metrics_.selected_session_kind == ReceiverSessionKind::kUnicore)
  {
    const auto& child = unicore_session_.metrics();
    metrics_.runtime_updates = child.runtime_updates;
    metrics_.malformed_records =
        child.malformed_lines + child.malformed_frames + child.records_rejected;
    metrics_.unknown_records = child.unknown_records;
    return;
  }

  metrics_.runtime_updates = 0u;
  metrics_.malformed_records = 0u;
  metrics_.unknown_records = 0u;
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
    default:
      return "unknown";
  }
}

}  // namespace universal_gnss_driver
