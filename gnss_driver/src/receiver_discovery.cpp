#include "universal_gnss_driver/receiver_discovery.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "unicore_ascii_validation.hpp"
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

struct NmeaDetectionCounts
{
  DetectionCounts runtime_sentences{};
  std::size_t gga_sentences{0u};
};

struct UnicoreAsciiDetectionCounts
{
  DetectionCounts supported_records{};
  std::size_t rtkstatusa_records{0u};
  std::size_t pvtslna_records{0u};
};

bool StartsWith(const std::string& text, const std::string_view prefix)
{
  return text.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), text.begin());
}

bool IsVersionToken(const std::string_view value)
{
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
           return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.';
         });
}

std::optional<ReceiverIdentityMetadata> FindUnicoreVersionAMetadata(
    const std::vector<std::uint8_t>& bytes)
{
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  constexpr std::string_view kPrefix = "#VERSIONA,";
  std::size_t line_start = 0u;
  while (line_start < text.size())
  {
    const std::size_t line_end = text.find_first_of("\r\n", line_start);
    const std::string_view line = text.substr(
        line_start, line_end == std::string_view::npos ? text.size() - line_start
                                                        : line_end - line_start);
    if (line.substr(0u, kPrefix.size()) == kPrefix)
    {
      const std::size_t header_end = line.find(';', kPrefix.size());
      if (header_end != std::string_view::npos)
      {
        const std::string_view fields = line.substr(header_end + 1u);
        if (fields.size() >= 5u && fields.front() == '"')
        {
          const std::size_t model_end = fields.find('"', 1u);
          if (model_end != std::string_view::npos && model_end + 2u < fields.size() &&
              fields[model_end + 1u] == ',' && fields[model_end + 2u] == '"')
          {
            const std::size_t firmware_start = model_end + 2u;
            const std::size_t firmware_end = fields.find('"', firmware_start + 1u);
            if (firmware_end != std::string_view::npos)
            {
              const std::string_view model = fields.substr(1u, model_end - 1u);
              const std::string_view firmware =
                  fields.substr(firmware_start + 1u, firmware_end - firmware_start - 1u);
              if (IsVersionToken(model) && IsVersionToken(firmware))
              {
                ReceiverIdentityMetadata metadata;
                metadata.model = std::string(model);
                metadata.firmware_version = std::string(firmware);
                return metadata;
              }
            }
          }
        }
      }
    }

    if (line_end == std::string_view::npos)
    {
      break;
    }
    line_start = line_end + 1u;
  }
  return std::nullopt;
}

void PopulateObservedUnicoreIdentity(ReceiverProbeResult& result,
                                     const std::vector<std::uint8_t>& bytes)
{
  if (const auto metadata = FindUnicoreVersionAMetadata(bytes); metadata.has_value())
  {
    result.identity = *metadata;
  }
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
    case ReceiverPortSource::kPlatformUart:
      return 3;
    case ReceiverPortSource::kExplicitPath:
      return 4;
  }

  return 5;
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

bool IsKnownGnssNmeaTalker(const std::string_view talker)
{
  return talker == "GP" || talker == "GL" || talker == "GA" || talker == "GB" ||
         talker == "BD" || talker == "GQ" || talker == "GN";
}

bool IsRuntimeGnssNmeaSentenceType(const std::string_view sentence_type)
{
  return sentence_type == "GGA" || sentence_type == "RMC" || sentence_type == "GSA" ||
         sentence_type == "GSV" || sentence_type == "GST" || sentence_type == "VTG" ||
         sentence_type == "ZDA" || sentence_type == "GLL";
}

bool IsLikelyReceiverNmeaSentence(const NmeaSentence& sentence)
{
  return !sentence.talker.empty() &&
         IsKnownGnssNmeaTalker(sentence.talker) &&
         IsRuntimeGnssNmeaSentenceType(sentence.sentence_type) &&
         sentence.checksum_status == ChecksumStatus::kValid;
}

bool IsValidGgaSentence(const NmeaSentence& sentence)
{
  return IsLikelyReceiverNmeaSentence(sentence) && sentence.sentence_type == "GGA";
}

NmeaDetectionCounts CountNmeaRecords(const std::vector<std::uint8_t>& bytes)
{
  NmeaDetectionCounts counts;
  NmeaSentenceFramer framer;
  for (std::size_t index = 0u; index < bytes.size(); ++index)
  {
    const auto result = framer.PushByte(bytes[index]);
    if (result.status != ParserStatus::kRecordReady || !result.record.has_value() ||
        !IsLikelyReceiverNmeaSentence(*result.record))
    {
      continue;
    }

    ++counts.runtime_sentences.count;
    if (!counts.runtime_sentences.earliest_bytes_consumed.has_value())
    {
      counts.runtime_sentences.earliest_bytes_consumed = index + 1u;
    }

    if (IsValidGgaSentence(*result.record))
    {
      ++counts.gga_sentences;
    }
  }

  return counts;
}

UnicoreAsciiDetectionCounts CountUnicoreAsciiRecords(
    const std::vector<std::uint8_t>& bytes)
{
  UnicoreAsciiDetectionCounts counts;
  UnicoreFrameFramer framer;
  for (std::size_t index = 0u; index < bytes.size(); ++index)
  {
    const auto result = framer.PushByte(bytes[index]);
    if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
    {
      continue;
    }

    const auto& frame = *result.record;
    if (!detail::IsVerifiedUnicoreAsciiRecord(frame))
    {
      continue;
    }

    ++counts.supported_records.count;
    if (!counts.supported_records.earliest_bytes_consumed.has_value())
    {
      counts.supported_records.earliest_bytes_consumed = index + 1u;
    }

    if (frame.message_name == "RTKSTATUSA")
    {
      ++counts.rtkstatusa_records;
    }
    if (frame.message_name == "PVTSLNA")
    {
      ++counts.pvtslna_records;
    }
  }

  return counts;
}

std::size_t CountMavlinkHeartbeats(const std::vector<std::uint8_t>& bytes)
{
  std::size_t count = 0u;
  for (std::size_t index = 0u; index + 8u < bytes.size(); ++index)
  {
    if (bytes[index] == 0xFEu)
    {
      const std::uint8_t payload_length = bytes[index + 1u];
      const std::uint8_t message_id = bytes[index + 5u];
      if (payload_length == 9u && message_id == 0u &&
          index + 6u + payload_length + 2u <= bytes.size())
      {
        ++count;
      }
    }
    else if (bytes[index] == 0xFDu && index + 9u < bytes.size())
    {
      const std::uint8_t payload_length = bytes[index + 1u];
      const std::uint32_t message_id =
          static_cast<std::uint32_t>(bytes[index + 7u]) |
          (static_cast<std::uint32_t>(bytes[index + 8u]) << 8u) |
          (static_cast<std::uint32_t>(bytes[index + 9u]) << 16u);
      if (payload_length == 9u && message_id == 0u &&
          index + 10u + payload_length + 2u <= bytes.size())
      {
        ++count;
      }
    }
  }

  return count;
}

bool LooksLikeRandomAsciiText(const std::vector<std::uint8_t>& bytes)
{
  if (bytes.size() < 8u)
  {
    return false;
  }

  std::size_t printable = 0u;
  std::size_t line_breaks = 0u;
  std::size_t gnss_leaders = 0u;
  for (const auto byte : bytes)
  {
    if (byte == '\r' || byte == '\n')
    {
      ++printable;
      ++line_breaks;
      continue;
    }
    if (byte == '$' || byte == '#')
    {
      ++gnss_leaders;
    }
    if (std::isprint(static_cast<unsigned char>(byte)) != 0 ||
        byte == '\t')
    {
      ++printable;
    }
  }

  return line_breaks > 0u && gnss_leaders == 0u &&
         printable * 100u >= bytes.size() * 80u;
}

ReceiverProbeConfidence ConfidenceFromScore(const int score)
{
  if (score >= 100)
  {
    return ReceiverProbeConfidence::kHigh;
  }
  if (score >= 20)
  {
    return ReceiverProbeConfidence::kMedium;
  }
  if (score > 0)
  {
    return ReceiverProbeConfidence::kLow;
  }
  return ReceiverProbeConfidence::kNone;
}

void AppendReasonToken(std::ostringstream& stream, const std::string_view token)
{
  if (stream.tellp() > 0)
  {
    stream << ',';
  }
  stream << token;
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

bool MeetsConfidenceThreshold(const ReceiverProbeResult& result,
                              const ReceiverProbeConfig& config)
{
  return result.discovery_score >= config.confidence_threshold_score;
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
  if (candidate.discovery_score != current_best.discovery_score)
  {
    return candidate.discovery_score > current_best.discovery_score;
  }

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
      result.reason = result.note;
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
  return DiscoverSerialPorts(ReceiverProbeConfig{}, paths);
}

std::vector<ReceiverPortCandidate> DiscoverSerialPorts(const ReceiverProbeConfig& config,
                                                       const ReceiverDiscoveryPaths& paths)
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

  if (config.include_platform_uarts)
  {
    for (const auto& platform_path : config.platform_uart_paths)
    {
      try
      {
        if (!fs::exists(platform_path))
        {
          continue;
        }
      }
      catch (const fs::filesystem_error&)
      {
        continue;
      }

      ReceiverPortCandidate candidate;
      candidate.path = platform_path;
      candidate.source = ReceiverPortSource::kPlatformUart;
      candidate.transport_type = ReceiverTransportType::kSerial;
      MaybeInsertPreferredCandidate(deduplicated, candidate);
    }

    for (const auto& prefix : config.platform_uart_prefixes)
    {
      for (const auto& platform_uart_path : ListMatchingDevicePaths(paths.dev_dir, prefix))
      {
        ReceiverPortCandidate candidate;
        candidate.path = platform_uart_path;
        candidate.source = ReceiverPortSource::kPlatformUart;
        candidate.transport_type = ReceiverTransportType::kSerial;
        MaybeInsertPreferredCandidate(deduplicated, candidate);
      }
    }
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
  if (bytes.empty())
  {
    result.note = "no_data";
    result.reason = result.note;
    return result;
  }

  const auto nmea_counts = CountNmeaRecords(bytes);
  result.evidence.nmea_sentences_seen = nmea_counts.runtime_sentences.count;

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

  const auto unicore_ascii_counts = CountUnicoreAsciiRecords(bytes);
  result.evidence.unicore_ascii_seen = unicore_ascii_counts.supported_records.count;

  const auto unicore_binary_counts =
      CountDetectedRecords<UnicoreBinaryFrameFramer, UnicoreBinaryFrame>(
          bytes,
          [](const UnicoreBinaryFrame& frame) {
            return frame.checksum_status == ChecksumStatus::kValid;
          });
  result.evidence.unicore_binary_seen = unicore_binary_counts.count;
  result.evidence.mavlink_heartbeats_seen = CountMavlinkHeartbeats(bytes);
  if (LooksLikeRandomAsciiText(bytes))
  {
    result.evidence.random_ascii_bytes_seen = bytes.size();
  }

  const StreamDetector detector;
  const auto earliest_detection = detector.Detect(bytes);

  const int ubx_score = static_cast<int>(result.evidence.ubx_frames_seen) * 100;
  const int unicore_score =
      static_cast<int>(unicore_ascii_counts.rtkstatusa_records) * 100 +
      static_cast<int>(unicore_ascii_counts.pvtslna_records) * 100 +
      static_cast<int>(result.evidence.unicore_ascii_seen -
                       unicore_ascii_counts.rtkstatusa_records -
                       unicore_ascii_counts.pvtslna_records) *
          100 +
      static_cast<int>(result.evidence.unicore_binary_seen) * 100;
  const int nmea_score =
      static_cast<int>(nmea_counts.gga_sentences) * 20 +
      static_cast<int>(result.evidence.nmea_sentences_seen - nmea_counts.gga_sentences) *
          10;
  const int mavlink_penalty =
      static_cast<int>(result.evidence.mavlink_heartbeats_seen) * 200;
  const int random_ascii_penalty =
      result.evidence.random_ascii_bytes_seen > 0u ? 50 : 0;

  const int positive_score = std::max({ubx_score, unicore_score, nmea_score, 0});
  result.discovery_score = positive_score - mavlink_penalty - random_ascii_penalty;

  std::ostringstream reason;
  if (result.evidence.ubx_frames_seen > 0u)
  {
    AppendReasonToken(reason, "valid_ubx_frame:+100");
  }
  if (unicore_ascii_counts.rtkstatusa_records > 0u)
  {
    AppendReasonToken(reason, "RTKSTATUSA:+100");
  }
  if (unicore_ascii_counts.pvtslna_records > 0u)
  {
    AppendReasonToken(reason, "PVTSLNA:+100");
  }
  if (result.evidence.unicore_ascii_seen >
      unicore_ascii_counts.rtkstatusa_records + unicore_ascii_counts.pvtslna_records)
  {
    AppendReasonToken(reason, "unicore_ascii:+100");
  }
  if (result.evidence.unicore_binary_seen > 0u)
  {
    AppendReasonToken(reason, "unicore_binary:+100");
  }
  if (nmea_counts.gga_sentences > 0u)
  {
    AppendReasonToken(reason, "valid_GGA:+20");
  }
  if (result.evidence.nmea_sentences_seen > nmea_counts.gga_sentences)
  {
    AppendReasonToken(reason, "valid_NMEA:+10");
  }
  if (result.evidence.mavlink_heartbeats_seen > 0u)
  {
    AppendReasonToken(reason, "MAVLink_heartbeat:-200");
  }
  if (result.evidence.random_ascii_bytes_seen > 0u)
  {
    AppendReasonToken(reason, "random_ascii:-50");
  }
  result.reason = reason.str();

  if (result.evidence.mavlink_heartbeats_seen > 0u)
  {
    result.detected_family = ReceiverDetectedFamily::kUnknown;
    result.confidence = ReceiverProbeConfidence::kNone;
    result.note = "mavlink_heartbeat_detected";
    if (result.reason.empty())
    {
      result.reason = result.note;
    }
    return result;
  }

  if (result.evidence.ubx_frames_seen > 0u &&
      !(result.evidence.unicore_ascii_seen > 0u || result.evidence.unicore_binary_seen > 0u))
  {
    result.detected_family = ReceiverDetectedFamily::kUblox;
    result.confidence = ConfidenceFromScore(result.discovery_score);
    return result;
  }

  if ((result.evidence.unicore_ascii_seen > 0u || result.evidence.unicore_binary_seen > 0u) &&
      result.evidence.ubx_frames_seen == 0u)
  {
    result.detected_family = ReceiverDetectedFamily::kUnicore;
    result.confidence = ConfidenceFromScore(result.discovery_score);
    PopulateObservedUnicoreIdentity(result, bytes);
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
      PopulateObservedUnicoreIdentity(result, bytes);
    }
    result.confidence = ConfidenceFromScore(result.discovery_score);
    result.note = "mixed_vendor_evidence_selected_earliest";
    return result;
  }

  if (result.evidence.nmea_sentences_seen > 0u && config.allow_generic_nmea_fallback)
  {
    result.detected_family = ReceiverDetectedFamily::kNmea;
    result.confidence = ConfidenceFromScore(result.discovery_score);
    return result;
  }

  if (result.evidence.random_ascii_bytes_seen > 0u)
  {
    result.note = "random_ascii_text";
    result.confidence = ReceiverProbeConfidence::kNone;
    if (result.reason.empty())
    {
      result.reason = result.note;
    }
    return result;
  }

  SetUnknownResultNote(result.evidence,
                       config.allow_generic_nmea_fallback,
                       result.note,
                       result.confidence);
  if (result.reason.empty())
  {
    result.reason = result.note;
  }
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

    if (IsHighConfidence(best_result) && MeetsConfidenceThreshold(best_result, config))
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
    candidates = DiscoverSerialPorts(config, paths);
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

    if (lhs.discovery_score != rhs.discovery_score)
    {
      return lhs.discovery_score > rhs.discovery_score;
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
    case ReceiverPortSource::kPlatformUart:
      return "platform_uart";
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
