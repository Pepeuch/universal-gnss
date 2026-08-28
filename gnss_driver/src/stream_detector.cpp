#include "universal_gnss_driver/stream_detector.hpp"

#include <optional>

#include "unicore_ascii_validation.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/unicore_framer.hpp"

namespace universal_gnss_driver
{

namespace
{

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::NmeaSentenceFramer;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::RtcmFrame;
using universal_gnss_protocols::RtcmFrameFramer;
using universal_gnss_protocols::UnicoreBinaryFrame;
using universal_gnss_protocols::UnicoreBinaryFrameFramer;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_protocols::UnicoreFrame;
using universal_gnss_protocols::UnicoreFrameFramer;

struct DetectionCandidate
{
  DetectedStreamProtocol protocol{DetectedStreamProtocol::kUnknown};
  std::size_t bytes_consumed{0};
  std::size_t frame_length_bytes{0};
};

template <typename FramerT, typename RecordT, typename AcceptFn>
std::optional<DetectionCandidate> DetectWithFramer(FramerT& framer,
                                                   const std::uint8_t* data,
                                                   const std::size_t size,
                                                   const DetectedStreamProtocol protocol,
                                                   AcceptFn accept)
{
  for (std::size_t index = 0; index < size; ++index)
  {
    const auto result = framer.PushByte(data[index]);
    if (result.status != ParserStatus::kRecordReady || !result.record.has_value())
    {
      continue;
    }

    const RecordT& record = *result.record;
    if (!accept(record))
    {
      continue;
    }

    return DetectionCandidate{protocol, index + 1u, record.raw_bytes.size()};
  }

  return std::nullopt;
}

void MaybeSelectEarlierCandidate(const std::optional<DetectionCandidate>& candidate,
                                 StreamDetectionResult& best_result)
{
  if (!candidate.has_value())
  {
    return;
  }

  if (best_result.protocol == DetectedStreamProtocol::kUnknown ||
      candidate->bytes_consumed < best_result.bytes_consumed)
  {
    best_result.protocol = candidate->protocol;
    best_result.bytes_consumed = candidate->bytes_consumed;
    best_result.frame_length_bytes = candidate->frame_length_bytes;
  }
}

}  // namespace

StreamDetectionResult StreamDetector::Detect(const std::uint8_t* data, const std::size_t size) const
{
  StreamDetectionResult best_result;
  if (data == nullptr || size == 0u)
  {
    return best_result;
  }

  NmeaSentenceFramer nmea_framer;
  const auto nmea_candidate = DetectWithFramer<NmeaSentenceFramer, NmeaSentence>(
      nmea_framer,
      data,
      size,
      DetectedStreamProtocol::kNmea,
      [](const NmeaSentence& sentence) {
        return !sentence.sentence_type.empty() &&
               sentence.checksum_status != ChecksumStatus::kInvalid;
      });
  MaybeSelectEarlierCandidate(nmea_candidate, best_result);

  UbxFrameFramer ubx_framer;
  const auto ubx_candidate = DetectWithFramer<UbxFrameFramer, UbxFrame>(
      ubx_framer,
      data,
      size,
      DetectedStreamProtocol::kUbx,
      [](const UbxFrame& frame) {
        return frame.checksum_status == ChecksumStatus::kValid;
      });
  MaybeSelectEarlierCandidate(ubx_candidate, best_result);

  RtcmFrameFramer rtcm_framer;
  const auto rtcm_candidate = DetectWithFramer<RtcmFrameFramer, RtcmFrame>(
      rtcm_framer,
      data,
      size,
      DetectedStreamProtocol::kRtcm3,
      [](const RtcmFrame& frame) {
        return frame.checksum_status == ChecksumStatus::kValid;
      });
  MaybeSelectEarlierCandidate(rtcm_candidate, best_result);

  UnicoreFrameFramer unicore_framer;
  const auto unicore_candidate = DetectWithFramer<UnicoreFrameFramer, UnicoreFrame>(
      unicore_framer,
      data,
      size,
      DetectedStreamProtocol::kUnicoreAscii,
      [](const UnicoreFrame& frame) {
        return detail::IsVerifiedUnicoreAsciiRecord(frame);
      });
  MaybeSelectEarlierCandidate(unicore_candidate, best_result);

  UnicoreBinaryFrameFramer unicore_binary_framer;
  const auto unicore_binary_candidate =
      DetectWithFramer<UnicoreBinaryFrameFramer, UnicoreBinaryFrame>(
          unicore_binary_framer,
          data,
          size,
          DetectedStreamProtocol::kUnicoreBinary,
          [](const UnicoreBinaryFrame& frame) {
            return frame.checksum_status == ChecksumStatus::kValid;
          });
  MaybeSelectEarlierCandidate(unicore_binary_candidate, best_result);

  return best_result;
}

StreamDetectionResult StreamDetector::Detect(const std::vector<std::uint8_t>& bytes) const
{
  return Detect(bytes.data(), bytes.size());
}

const char* ToString(const DetectedStreamProtocol protocol)
{
  switch (protocol)
  {
    case DetectedStreamProtocol::kNmea:
      return "nmea";
    case DetectedStreamProtocol::kUbx:
      return "ubx";
    case DetectedStreamProtocol::kRtcm3:
      return "rtcm3";
    case DetectedStreamProtocol::kUnicoreAscii:
      return "unicore_ascii";
    case DetectedStreamProtocol::kUnicoreBinary:
      return "unicore_binary";
    case DetectedStreamProtocol::kUnknown:
    default:
      return "unknown";
  }
}

}  // namespace universal_gnss_driver
