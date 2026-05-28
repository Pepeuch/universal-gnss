#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/protocol_type.hpp"
#include "universal_gnss_protocols/rtcm_records.hpp"

namespace universal_gnss_tools
{

struct GnssStreamInspectionItem
{
  std::size_t item_index{0};
  std::size_t byte_offset{0};
  std::size_t length_bytes{0};
  universal_gnss_protocols::ProtocolType protocol{
      universal_gnss_protocols::ProtocolType::kUnknown};
  universal_gnss_protocols::ChecksumStatus checksum_status{
      universal_gnss_protocols::ChecksumStatus::kNotChecked};

  std::string identity{};
  std::string classification{};

  std::string nmea_talker{};
  std::string nmea_sentence_type{};

  std::uint8_t ubx_class_id{0};
  std::uint8_t ubx_message_id{0};
  std::string ubx_message_name{};

  std::uint16_t rtcm_message_type{0};
  universal_gnss_protocols::RtcmMessageInfo rtcm_message_info{};
};

struct GnssStreamInspectionSummary
{
  std::size_t total_bytes_read{0};
  std::size_t total_items_found{0};
  std::size_t valid_items{0};
  std::size_t invalid_items{0};
  std::size_t malformed_events{0};
  std::size_t truncated_items{0};
  std::size_t noise_bytes{0};
  std::size_t noise_spans{0};

  std::map<std::string, std::size_t> counts_by_protocol{};
  std::map<std::string, std::size_t> counts_by_nmea_sentence_type{};
  std::map<std::string, std::size_t> counts_by_ubx_message{};
  std::map<std::string, std::size_t> counts_by_unicore_message{};
  std::map<std::uint16_t, std::size_t> counts_by_rtcm_message_type{};
  std::map<universal_gnss_protocols::RtcmConstellation, std::size_t>
      rtcm_msm_counts_by_constellation{};
};

struct GnssStreamInspectionResult
{
  std::vector<GnssStreamInspectionItem> items{};
  GnssStreamInspectionSummary summary{};
};

GnssStreamInspectionResult InspectGnssStreamBytes(
    const std::vector<std::uint8_t>& bytes,
    bool include_items = true);

GnssStreamInspectionResult InspectGnssStreamStream(
    std::istream& input,
    bool include_items = true);

const char* DescribeProtocolType(universal_gnss_protocols::ProtocolType protocol);

std::string FormatUbxMessageKey(std::uint8_t class_id, std::uint8_t message_id);

std::string DescribeUbxMessage(std::uint8_t class_id, std::uint8_t message_id);

std::string FormatGnssStreamInspectionText(
    const GnssStreamInspectionResult& result,
    bool summary_only = false);

std::string FormatGnssStreamInspectionJson(
    const GnssStreamInspectionResult& result,
    bool summary_only = false);

}  // namespace universal_gnss_tools
