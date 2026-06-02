#include "universal_gnss_driver/receiver_discovery.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "universal_gnss_driver/stream_detector.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/unicore_framer.hpp"
#include "universal_gnss_transport/posix_serial_transport.hpp"

namespace universal_gnss_driver
{

namespace
{

namespace fs = std::filesystem;

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::NmeaSentenceFramer;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::RtcmFrame;
using universal_gnss_protocols::RtcmFrameFramer;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_protocols::UnicoreBinaryFrame;
using universal_gnss_protocols::UnicoreBinaryFrameFramer;
using universal_gnss_protocols::UnicoreFrame;
using universal_gnss_protocols::UnicoreFrameFramer;
using universal_gnss_transport::PosixSerialConfig;
using universal_gnss_transport::PosixSerialTransport;
using universal_gnss_transport::TransportError;
using universal_gnss_transport::TransportStatus;

struct DetectionCounts
{
  std::size_t count{0u};
  std::optional<std::size_t> earliest_bytes_consumed{};
};

bool StartsWith(const std::string& text, const std::string_view prefix)
{
  return text.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), text.begin());
}

std::string CanonicalKey(const std::string& path)
{
  try
  {
    return fs::weakly_canonical(fs::path(path)).string();
  }
  catch (const fs::filesystem_error&)
  {
    try
    {
      return fs::absolute(fs::path(path)).lexically_normal().string();
    }
    catch (const fs::filesystem_error&)
    {
      return path;
    }
  }
}

std::vector<std::string> ListDirectoryPaths(const std::string& directory)
{
  std::vector<std::string> entries;
  try
  {
    if (!fs::exists(directory))
    {
      return entries;
    }

    for (const auto& entry : fs::directory_iterator(directory))
    {
      entries.push_back(entry.path().string());
    }
  }
  catch (const fs::filesystem_error&)
  {
    return {};
  }

  std::sort(entries.begin(), entries.end());
  return entries;
}

std::vector<std::string> ListMatchingDevicePaths(const std::string& dev_dir,
                                                 const std::string_view prefix)
{
  std::vector<std::string> matches;
  for (const auto& entry_path : ListDirectoryPaths(dev_dir))
  {
    const auto filename = fs::path(entry_path).filename().string();
    if (StartsWith(filename, prefix))
    {
      matches.push_back(entry_path);
    }
  }

  std::sort(matches.begin(), matches.end());
  return matches;
}

int SourcePriority(const ReceiverPortSource source)
{
  switch (source)
  {
    case ReceiverPortSource::kSerialById:
      return 0;
    case ReceiverPortSource::kTtyAcm:
      return 1;
    case ReceiverPortSource::kTtyUsb:
      return 2;
    case ReceiverPortSource::kExplicitPath:
      return 3;
  }

  return 4;
}

void MaybeInsertPreferredCandidate(std::map<std::string, ReceiverPortCandidate>& deduplicated,
                                   const ReceiverPortCandidate& candidate)
{
  const std::string key = CanonicalKey(candidate.path);
  const auto existing = deduplicated.find(key);
  if (existing == deduplicated.end())
  {
    deduplicated.emplace(key, candidate);
    return;
  }

  const int existing_priority = SourcePriority(existing->second.source);
  const int candidate_priority = SourcePriority(candidate.source);
  if (candidate_priority < existing_priority ||
      (candidate_priority == existing_priority && candidate.path < existing->second.path))
  {
    existing->second = candidate;
  }
}

bool IsSupportedUnicoreAsciiName(const std::string_view name)
{
  return name == "PVTSLNA" || name == "BESTNAVA" || name == "RTKSTATUSA" ||
         name == "RTCMSTATUSA" || name == "SATSINFOA" || name == "BESTSATA" ||
         name == "JAMSTATUSA" || name == "FREQJAMSTATUSA" || name == "HWSTATUSA" ||
         name == "AGCA";
}

template <typename FramerT, typename RecordT, typename AcceptFn>
DetectionCounts CountDetectedRecords(const std::vector<std::uint8_t>& bytes, AcceptFn&& accept)
{
  DetectionCounts counts;
  FramerT framer;
  for (std::size_t index = 0u; index < bytes.size(); ++index)
  {
    const auto result = framer.PushByte(bytes[index]);
    if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
    {
      continue;
    }

    if (!accept(*result.record))
    {
      continue;
    }

    ++counts.count;
    if (!counts.earliest_bytes_consumed.has_value())
    {
      counts.earliest_bytes_consumed = index + 1u;
    }
  }

  return counts;
}

ReceiverProbeConfidence MaxConfidence(const ReceiverProbeConfidence lhs,
                                      const ReceiverProbeConfidence rhs)
{
  return static_cast<int>(lhs) >= static_cast<int>(rhs) ? lhs : rhs;
}

void SetUnknownResultNote(const ReceiverProbeEvidence& evidence,
                          const bool allow_generic_nmea_fallback,
                          std::string& note,
                          ReceiverProbeConfidence& confidence)
{
  if (evidence.rtcm_frames_seen > 0u && evidence.ubx_frames_seen == 0u &&
      evidence.unicore_ascii_seen == 0u && evidence.unicore_binary_seen == 0u &&
      evidence.nmea_sentences_seen == 0u)
  {
    note = "rtcm_only_stream";
    confidence = ReceiverProbeConfidence::kLow;
    return;
  }

  if (evidence.nmea_sentences_seen > 0u && !allow_generic_nmea_fallback &&
      evidence.ubx_frames_seen == 0u && evidence.unicore_ascii_seen == 0u &&
      evidence.unicore_binary_seen == 0u)
  {
    note = "nmea_only_fallback_disabled";
    confidence = ReceiverProbeConfidence::kLow;
    return;
  }

  note = "no_recognizable_receiver_frames";
  confidence = ReceiverProbeConfidence::kNone;
}

bool IsHighConfidence(const ReceiverProbeResult& result)
{
  return result.confidence == ReceiverProbeConfidence::kHigh;
}

int ConfidenceRank(const ReceiverProbeConfidence confidence)
{
  return static_cast<int>(confidence);
}

int FamilyRank(const ReceiverDetectedFamily family)
{
  switch (family)
  {
    case ReceiverDetectedFamily::kUblox:
    case ReceiverDetectedFamily::kUnicore:
    case ReceiverDetectedFamily::kNmea:
      return 1;
    case ReceiverDetectedFamily::kUnknown:
    default:
      return 0;
  }
}

bool IsBetterProbeResult(const ReceiverProbeResult& candidate,
                         const ReceiverProbeResult& current_best)
{
  if (ConfidenceRank(candidate.confidence) != ConfidenceRank(current_best.confidence))
  {
    return ConfidenceRank(candidate.confidence) > ConfidenceRank(current_best.confidence);
  }

  if (FamilyRank(candidate.detected_family) != FamilyRank(current_best.detected_family))
  {
    return FamilyRank(candidate.detected_family) > FamilyRank(current_best.detected_family);
  }

  const std::size_t candidate_vendor_evidence =
      candidate.evidence.ubx_frames_seen + candidate.evidence.unicore_ascii_seen +
      candidate.evidence.unicore_binary_seen + candidate.evidence.nmea_sentences_seen;
  const std::size_t current_vendor_evidence =
      current_best.evidence.ubx_frames_seen + current_best.evidence.unicore_ascii_seen +
      current_best.evidence.unicore_binary_seen + current_best.evidence.nmea_sentences_seen;
  if (candidate_vendor_evidence != current_vendor_evidence)
  {
    return candidate_vendor_evidence > current_vendor_evidence;
  }

  if (candidate.evidence.bytes_read != current_best.evidence.bytes_read)
  {
    return candidate.evidence.bytes_read > current_best.evidence.bytes_read;
  }

  if (candidate.selected_baud.has_value() != current_best.selected_baud.has_value())
  {
    return candidate.selected_baud.has_value();
  }

  if (candidate.selected_baud.has_value() && current_best.selected_baud.has_value() &&
      *candidate.selected_baud != *current_best.selected_baud)
  {
    return *candidate.selected_baud > *current_best.selected_baud;
  }

  return false;
}

ReceiverProbeResult MakeBaseProbeResult(const ReceiverPortCandidate& candidate)
{
  ReceiverProbeResult result;
  result.path = candidate.path;
  result.stable_id = candidate.stable_id;
  result.transport_type = candidate.transport_type;
  result.source = candidate.source;
  return result;
}

const char* ToString(const TransportError error)
{
  switch (error)
  {
    case TransportError::kNone:
      return "none";
    case TransportError::kClosed:
      return "closed";
    case TransportError::kInvalidArgument:
      return "invalid_argument";
    case TransportError::kOverflow:
      return "overflow";
    case TransportError::kReadFailure:
      return "read_failure";
    case TransportError::kWriteFailure:
      return "write_failure";
    case TransportError::kUnsupported:
      return "unsupported";
    case TransportError::kUnknown:
    default:
      return "unknown";
  }
}

ReceiverProbeResult ProbeSerialPortAtBaud(const ReceiverPortCandidate& candidate,
                                          const std::uint32_t baud_rate,
                                          const ReceiverProbeConfig& config)
{
  ReceiverProbeResult result = MakeBaseProbeResult(candidate);
  result.selected_baud = baud_rate;

#if defined(__linux__)
  const std::uint32_t effective_timeout_ms = config.read_timeout_ms > 0u ? config.read_timeout_ms : 250u;
  PosixSerialTransport transport;
  const auto open_error = transport.Open(PosixSerialConfig{
      candidate.path,
      baud_rate,
      false,
      effective_timeout_ms,
  });
  if (open_error != TransportError::kNone)
  {
    result.note = std::string("open_failed:") + ToString(open_error);
    return result;
  }

  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(static_cast<int>(effective_timeout_ms * 3u));

  std::array<std::uint8_t, 512u> buffer{};
  std::vector<std::uint8_t> bytes;
  bytes.reserve(config.max_probe_bytes);
  std::size_t idle_reads = 0u;
  while (bytes.size() < config.max_probe_bytes && std::chrono::steady_clock::now() < deadline)
  {
    const auto read = transport.Read(buffer.data(), std::min(buffer.size(), config.max_probe_bytes - bytes.size()));
    if (read.status == TransportStatus::kError)
    {
      result.note = std::string("read_failed:") + ToString(read.error);
      break;
    }

    if (read.bytes_read == 0u)
    {
      ++idle_reads;
      if (idle_reads >= 2u && !bytes.empty())
      {
        break;
      }
      continue;
    }

    idle_reads = 0u;
    bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read.bytes_read));
    result = AnalyzeReceiverProbeBytes(candidate, baud_rate, bytes, config);
    if (IsHighConfidence(result))
    {
      break;
    }
  }

  if (result.evidence.bytes_read == 0u)
  {
    result = AnalyzeReceiverProbeBytes(candidate, baud_rate, bytes, config);
    if (result.note.empty())
    {
      result.note = bytes.empty() ? "no_data" : result.note;
    }
  }

  transport.Close();
  return result;
#else
  (void)baud_rate;
  (void)config;
  result.note = "serial_probe_unsupported_on_this_platform";
  return result;
#endif
}

}  // namespace

std::vector<ReceiverPortCandidate> DiscoverSerialPorts(const ReceiverDiscoveryPaths& paths)
{
  std::map<std::string, ReceiverPortCandidate> deduplicated;

  for (const auto& by_id_path : ListDirectoryPaths(paths.serial_by_id_dir))
  {
    ReceiverPortCandidate candidate;
    candidate.path = by_id_path;
    candidate.stable_id = fs::path(by_id_path).filename().string();
    candidate.source = ReceiverPortSource::kSerialById;
    candidate.transport_type = ReceiverTransportType::kSerial;
    MaybeInsertPreferredCandidate(deduplicated, candidate);
  }

  for (const auto& tty_acm_path : ListMatchingDevicePaths(paths.dev_dir, "ttyACM"))
  {
    ReceiverPortCandidate candidate;
    candidate.path = tty_acm_path;
    candidate.source = ReceiverPortSource::kTtyAcm;
    candidate.transport_type = ReceiverTransportType::kSerial;
    MaybeInsertPreferredCandidate(deduplicated, candidate);
  }

  for (const auto& tty_usb_path : ListMatchingDevicePaths(paths.dev_dir, "ttyUSB"))
  {
    ReceiverPortCandidate candidate;
    candidate.path = tty_usb_path;
    candidate.source = ReceiverPortSource::kTtyUsb;
    candidate.transport_type = ReceiverTransportType::kSerial;
    MaybeInsertPreferredCandidate(deduplicated, candidate);
  }

  std::vector<ReceiverPortCandidate> candidates;
  candidates.reserve(deduplicated.size());
  for (const auto& entry : deduplicated)
  {
    candidates.push_back(entry.second);
  }

  std::sort(candidates.begin(), candidates.end(), [](const ReceiverPortCandidate& lhs,
                                                     const ReceiverPortCandidate& rhs) {
    const int lhs_priority = SourcePriority(lhs.source);
    const int rhs_priority = SourcePriority(rhs.source);
    if (lhs_priority != rhs_priority)
    {
      return lhs_priority < rhs_priority;
    }
    return lhs.path < rhs.path;
  });
  return candidates;
}

ReceiverPortCandidate MakeExplicitReceiverPortCandidate(const std::string& path,
                                                        const ReceiverDiscoveryPaths& paths)
{
  ReceiverPortCandidate candidate;
  candidate.path = path;
  candidate.transport_type = ReceiverTransportType::kSerial;
  candidate.source = ReceiverPortSource::kExplicitPath;

  const fs::path by_id_dir(paths.serial_by_id_dir);
  const fs::path candidate_path(path);
  if (candidate_path.has_parent_path() && candidate_path.parent_path() == by_id_dir)
  {
    candidate.stable_id = candidate_path.filename().string();
  }

  return candidate;
}

ReceiverProbeResult AnalyzeReceiverProbeBytes(const ReceiverPortCandidate& candidate,
                                              const std::uint32_t baud_rate,
                                              const std::vector<std::uint8_t>& bytes,
                                              const ReceiverProbeConfig& config)
{
  ReceiverProbeResult result = MakeBaseProbeResult(candidate);
  result.selected_baud = baud_rate;
  result.evidence.bytes_read = bytes.size();

  const auto nmea_counts = CountDetectedRecords<NmeaSentenceFramer, NmeaSentence>(
      bytes,
      [](const NmeaSentence& sentence) {
        return !sentence.sentence_type.empty() &&
               sentence.checksum_status != ChecksumStatus::kInvalid;
      });
  result.evidence.nmea_sentences_seen = nmea_counts.count;

  const auto ubx_counts = CountDetectedRecords<UbxFrameFramer, UbxFrame>(
      bytes,
      [](const UbxFrame& frame) {
        return frame.checksum_status == ChecksumStatus::kValid;
      });
  result.evidence.ubx_frames_seen = ubx_counts.count;

  const auto rtcm_counts = CountDetectedRecords<RtcmFrameFramer, RtcmFrame>(
      bytes,
      [](const RtcmFrame& frame) {
        return frame.checksum_status == ChecksumStatus::kValid;
      });
  result.evidence.rtcm_frames_seen = rtcm_counts.count;

  const auto unicore_ascii_counts = CountDetectedRecords<UnicoreFrameFramer, UnicoreFrame>(
      bytes,
      [](const UnicoreFrame& frame) {
        return frame.sync_char != '$' && IsSupportedUnicoreAsciiName(frame.message_name);
      });
  result.evidence.unicore_ascii_seen = unicore_ascii_counts.count;

  const auto unicore_binary_counts =
      CountDetectedRecords<UnicoreBinaryFrameFramer, UnicoreBinaryFrame>(
          bytes,
          [](const UnicoreBinaryFrame& frame) {
            return frame.checksum_status == ChecksumStatus::kValid;
          });
  result.evidence.unicore_binary_seen = unicore_binary_counts.count;

  const StreamDetector detector;
  const auto earliest_detection = detector.Detect(bytes);

  if (result.evidence.ubx_frames_seen > 0u &&
      !(result.evidence.unicore_ascii_seen > 0u || result.evidence.unicore_binary_seen > 0u))
  {
    result.detected_family = ReceiverDetectedFamily::kUblox;
    result.confidence = ReceiverProbeConfidence::kHigh;
    return result;
  }

  if ((result.evidence.unicore_ascii_seen > 0u || result.evidence.unicore_binary_seen > 0u) &&
      result.evidence.ubx_frames_seen == 0u)
  {
    result.detected_family = ReceiverDetectedFamily::kUnicore;
    result.confidence = ReceiverProbeConfidence::kHigh;
    return result;
  }

  if (result.evidence.ubx_frames_seen > 0u &&
      (result.evidence.unicore_ascii_seen > 0u || result.evidence.unicore_binary_seen > 0u))
  {
    if (earliest_detection.protocol == DetectedStreamProtocol::kUbx)
    {
      result.detected_family = ReceiverDetectedFamily::kUblox;
    }
    else
    {
      result.detected_family = ReceiverDetectedFamily::kUnicore;
    }
    result.confidence = ReceiverProbeConfidence::kHigh;
    result.note = "mixed_vendor_evidence_selected_earliest";
    return result;
  }

  if (result.evidence.nmea_sentences_seen > 0u && config.allow_generic_nmea_fallback)
  {
    result.detected_family = ReceiverDetectedFamily::kNmea;
    result.confidence = ReceiverProbeConfidence::kMedium;
    return result;
  }

  SetUnknownResultNote(result.evidence,
                       config.allow_generic_nmea_fallback,
                       result.note,
                       result.confidence);
  return result;
}

ReceiverProbeResult ProbeReceiverPort(const ReceiverPortCandidate& candidate,
                                      const ReceiverProbeConfig& config)
{
  ReceiverProbeResult best_result = MakeBaseProbeResult(candidate);
  if (config.baud_candidates.empty())
  {
    best_result.note = "no_baud_candidates";
    return best_result;
  }

  for (const auto baud_rate : config.baud_candidates)
  {
    const auto candidate_result = ProbeSerialPortAtBaud(candidate, baud_rate, config);
    if (IsBetterProbeResult(candidate_result, best_result))
    {
      best_result = candidate_result;
    }

    if (IsHighConfidence(best_result))
    {
      break;
    }
  }

  return best_result;
}

std::vector<ReceiverProbeResult> DiscoverReceivers(const ReceiverProbeConfig& config,
                                                   const std::optional<std::string>& explicit_path,
                                                   const ReceiverDiscoveryPaths& paths)
{
  std::vector<ReceiverPortCandidate> candidates;
  if (explicit_path.has_value())
  {
    candidates.push_back(MakeExplicitReceiverPortCandidate(*explicit_path, paths));
  }
  else
  {
    candidates = DiscoverSerialPorts(paths);
  }

  std::vector<ReceiverProbeResult> results;
  results.reserve(candidates.size());
  for (const auto& candidate : candidates)
  {
    results.push_back(ProbeReceiverPort(candidate, config));
  }

  return SortReceiverProbeResults(std::move(results));
}

std::vector<ReceiverProbeResult> SortReceiverProbeResults(std::vector<ReceiverProbeResult> results)
{
  std::sort(results.begin(), results.end(), [](const ReceiverProbeResult& lhs,
                                               const ReceiverProbeResult& rhs) {
    if (ConfidenceRank(lhs.confidence) != ConfidenceRank(rhs.confidence))
    {
      return ConfidenceRank(lhs.confidence) > ConfidenceRank(rhs.confidence);
    }

    if (FamilyRank(lhs.detected_family) != FamilyRank(rhs.detected_family))
    {
      return FamilyRank(lhs.detected_family) > FamilyRank(rhs.detected_family);
    }

    if (lhs.evidence.bytes_read != rhs.evidence.bytes_read)
    {
      return lhs.evidence.bytes_read > rhs.evidence.bytes_read;
    }

    const int lhs_priority = SourcePriority(lhs.source);
    const int rhs_priority = SourcePriority(rhs.source);
    if (lhs_priority != rhs_priority)
    {
      return lhs_priority < rhs_priority;
    }

    return lhs.path < rhs.path;
  });
  return results;
}

const char* ToString(const ReceiverTransportType transport_type)
{
  switch (transport_type)
  {
    case ReceiverTransportType::kSerial:
    default:
      return "serial";
  }
}

const char* ToString(const ReceiverPortSource source)
{
  switch (source)
  {
    case ReceiverPortSource::kSerialById:
      return "serial_by_id";
    case ReceiverPortSource::kTtyAcm:
      return "tty_acm";
    case ReceiverPortSource::kTtyUsb:
      return "tty_usb";
    case ReceiverPortSource::kExplicitPath:
    default:
      return "explicit_path";
  }
}

const char* ToString(const ReceiverDetectedFamily family)
{
  switch (family)
  {
    case ReceiverDetectedFamily::kUblox:
      return "ublox";
    case ReceiverDetectedFamily::kUnicore:
      return "unicore";
    case ReceiverDetectedFamily::kNmea:
      return "nmea";
    case ReceiverDetectedFamily::kUnknown:
    default:
      return "unknown";
  }
}

const char* ToString(const ReceiverProbeConfidence confidence)
{
  switch (confidence)
  {
    case ReceiverProbeConfidence::kLow:
      return "low";
    case ReceiverProbeConfidence::kMedium:
      return "medium";
    case ReceiverProbeConfidence::kHigh:
      return "high";
    case ReceiverProbeConfidence::kNone:
    default:
      return "none";
  }
}

}  // namespace universal_gnss_driver
