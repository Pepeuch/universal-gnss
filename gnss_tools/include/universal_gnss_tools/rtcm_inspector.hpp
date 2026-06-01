#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/rtcm_records.hpp"

namespace universal_gnss_tools
{

struct RtcmFrameInspection
{
  std::size_t frame_index{0};
  std::size_t byte_offset{0};
  std::size_t length_bytes{0};
  std::uint16_t message_type{0};
  universal_gnss_protocols::ChecksumStatus checksum_status{
      universal_gnss_protocols::ChecksumStatus::kMissing};
  universal_gnss_protocols::RtcmMessageInfo message_info{};
};

struct RtcmInspectionSummary
{
  std::size_t total_bytes_read{0};
  std::size_t total_frames_found{0};
  std::size_t valid_frames{0};
  std::size_t invalid_frames{0};
  std::size_t malformed_events{0};
  std::size_t truncated_frames{0};
  std::map<std::uint16_t, std::size_t> counts_by_message_type{};
  std::map<universal_gnss_protocols::RtcmConstellation, std::size_t>
      msm_counts_by_constellation{};
  std::optional<universal_gnss_protocols::RtcmBaseStationArpRecord> last_base_station_arp{};
};

struct RtcmInspectionResult
{
  std::vector<RtcmFrameInspection> frames{};
  RtcmInspectionSummary summary{};
};

RtcmInspectionResult InspectRtcmBytes(
    const std::vector<std::uint8_t>& bytes,
    bool include_frames = true);

RtcmInspectionResult InspectRtcmStream(std::istream& input, bool include_frames = true);

std::string DescribeRtcmConstellation(
    universal_gnss_protocols::RtcmConstellation constellation);

std::string DescribeRtcmMessageInfo(
    const universal_gnss_protocols::RtcmMessageInfo& message_info);

std::string DescribeChecksumStatus(universal_gnss_protocols::ChecksumStatus status);

std::string FormatRtcmInspectionText(
    const RtcmInspectionResult& result,
    bool summary_only = false);

std::string FormatRtcmInspectionJson(
    const RtcmInspectionResult& result,
    bool summary_only = false);

}  // namespace universal_gnss_tools
