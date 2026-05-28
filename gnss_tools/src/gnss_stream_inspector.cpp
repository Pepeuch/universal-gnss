#include "universal_gnss_tools/gnss_stream_inspector.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <optional>
#include <ostream>
#include <sstream>
#include <utility>

#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"
#include "universal_gnss_protocols/unicore_framer.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_tools/rtcm_inspector.hpp"

namespace universal_gnss_tools
{

namespace
{

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::NmeaSentenceFramer;
using universal_gnss_protocols::ParserResult;
    using universal_gnss_protocols::ParserStatus;
    using universal_gnss_protocols::ProtocolType;
    using universal_gnss_protocols::RtcmConstellation;
    using universal_gnss_protocols::RtcmFrame;
    using universal_gnss_protocols::RtcmFrameFramer;
    using universal_gnss_protocols::RtcmMessageInfo;
    using universal_gnss_protocols::UnicoreFrame;
    using universal_gnss_protocols::UnicoreFrameFramer;
    using universal_gnss_protocols::UbxFrame;
    using universal_gnss_protocols::UbxFrameFramer;

template <typename RecordT>
struct ProbeResult
{
  ParserStatus status{ParserStatus::kIdle};
  std::optional<RecordT> record{};
  std::size_t bytes_consumed{0};
};

template <typename FramerT, typename RecordT>
ProbeResult<RecordT> ProbeAtOffset(FramerT& framer,
                                   const std::vector<std::uint8_t>& bytes,
                                   const std::size_t start_offset)
{
  framer.Reset();
  for (std::size_t index = start_offset; index < bytes.size(); ++index)
  {
    auto parser_result = framer.PushByte(bytes[index]);
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

bool IsRecognizedNmeaSentence(const NmeaSentence& sentence)
{
  if (sentence.sentence_type.empty())
  {
    return false;
  }

  for (const char ch : sentence.sentence_type)
  {
    if (!std::isalnum(static_cast<unsigned char>(ch)))
    {
      return false;
    }
  }

  return true;
}

bool IsChecksumAccepted(const ChecksumStatus status)
{
  return status != ChecksumStatus::kInvalid;
}

void AddNoiseBytes(const std::size_t count,
                   GnssStreamInspectionSummary& summary,
                   bool& in_noise_span)
{
  if (count == 0u)
  {
    return;
  }

  summary.noise_bytes += count;
  if (!in_noise_span)
  {
    ++summary.noise_spans;
    in_noise_span = true;
  }
}

void EndNoiseSpan(bool& in_noise_span)
{
  in_noise_span = false;
}

std::string BuildNmeaIdentity(const NmeaSentence& sentence)
{
  return sentence.talker + sentence.sentence_type;
}

RtcmMessageInfo BuildRtcmMessageInfo(const RtcmFrame& frame)
{
  RtcmMessageInfo info;
  info.message_type = frame.message_type;
  info.is_station_arp = universal_gnss_protocols::IsRtcmStationArpMessage(info.message_type);
  info.is_glonass_bias = universal_gnss_protocols::IsRtcmGlonassBiasMessage(info.message_type);
  info.msm_constellation =
      universal_gnss_protocols::GetRtcmMsmConstellation(info.message_type);
  info.is_msm = info.msm_constellation != RtcmConstellation::kUnknown;
  return info;
}

GnssStreamInspectionItem MakeNmeaItem(const NmeaSentence& sentence,
                                      const std::size_t byte_offset,
                                      const std::size_t item_index)
{
  GnssStreamInspectionItem item;
  item.item_index = item_index;
  item.byte_offset = byte_offset;
  item.length_bytes = sentence.raw_bytes.size();
  item.protocol = ProtocolType::kNmea;
  item.checksum_status = sentence.checksum_status;
  item.identity = BuildNmeaIdentity(sentence);
  item.nmea_talker = sentence.talker;
  item.nmea_sentence_type = sentence.sentence_type;
  return item;
}

GnssStreamInspectionItem MakeUbxItem(const UbxFrame& frame,
                                     const std::size_t byte_offset,
                                     const std::size_t item_index)
{
  GnssStreamInspectionItem item;
  item.item_index = item_index;
  item.byte_offset = byte_offset;
  item.length_bytes = frame.raw_bytes.size();
  item.protocol = ProtocolType::kUbx;
  item.checksum_status = frame.checksum_status;
  item.ubx_class_id = frame.class_id;
  item.ubx_message_id = frame.message_id;
  item.identity = FormatUbxMessageKey(frame.class_id, frame.message_id);
  item.ubx_message_name = DescribeUbxMessage(frame.class_id, frame.message_id);
  item.classification = item.ubx_message_name;
  return item;
}

GnssStreamInspectionItem MakeRtcmItem(const RtcmFrame& frame,
                                      const std::size_t byte_offset,
                                      const std::size_t item_index)
{
  GnssStreamInspectionItem item;
  item.item_index = item_index;
  item.byte_offset = byte_offset;
  item.length_bytes = frame.raw_bytes.size();
  item.protocol = ProtocolType::kRtcm3;
  item.checksum_status = frame.checksum_status;
  item.rtcm_message_type = frame.message_type;
  item.identity = std::to_string(frame.message_type);
  item.rtcm_message_info = BuildRtcmMessageInfo(frame);
  item.classification = DescribeRtcmMessageInfo(item.rtcm_message_info);
  return item;
}

GnssStreamInspectionItem MakeUnicoreItem(const UnicoreFrame& frame,
                                         const std::size_t byte_offset,
                                         const std::size_t item_index)
{
  GnssStreamInspectionItem item;
  item.item_index = item_index;
  item.byte_offset = byte_offset;
  item.length_bytes = frame.raw_bytes.size();
  item.protocol = ProtocolType::kUnicore;
  item.checksum_status = frame.checksum_status;
  item.identity = frame.message_name;
  item.classification = frame.message_name;
  return item;
}

void AccumulateItem(const GnssStreamInspectionItem& item,
                    const bool include_items,
                    GnssStreamInspectionResult& result)
{
  ++result.summary.total_items_found;
  if (IsChecksumAccepted(item.checksum_status))
  {
    ++result.summary.valid_items;
  }
  else
  {
    ++result.summary.invalid_items;
  }

  ++result.summary.counts_by_protocol[DescribeProtocolType(item.protocol)];

  if (item.protocol == ProtocolType::kNmea && !item.nmea_sentence_type.empty())
  {
    ++result.summary.counts_by_nmea_sentence_type[item.nmea_sentence_type];
  }

  if (item.protocol == ProtocolType::kUbx)
  {
    ++result.summary.counts_by_ubx_message[FormatUbxMessageKey(
        item.ubx_class_id,
        item.ubx_message_id)];
  }

  if (item.protocol == ProtocolType::kUnicore && !item.identity.empty())
  {
    ++result.summary.counts_by_unicore_message[item.identity];
  }

  if (item.protocol == ProtocolType::kRtcm3)
  {
    ++result.summary.counts_by_rtcm_message_type[item.rtcm_message_type];
    if (item.rtcm_message_info.is_msm &&
        item.rtcm_message_info.msm_constellation != RtcmConstellation::kUnknown)
    {
      ++result.summary
            .rtcm_msm_counts_by_constellation[item.rtcm_message_info.msm_constellation];
    }
  }

  if (include_items)
  {
    result.items.push_back(item);
  }
}

bool ConsumeNmeaAtOffset(const std::vector<std::uint8_t>& bytes,
                         const std::size_t start_offset,
                         const bool include_items,
                         GnssStreamInspectionResult& result,
                         std::size_t& next_offset,
                         bool& stop_scan,
                         bool& in_noise_span)
{
  NmeaSentenceFramer framer;
  const ProbeResult<NmeaSentence> probe =
      ProbeAtOffset<NmeaSentenceFramer, NmeaSentence>(framer, bytes, start_offset);

  if (probe.status == ParserStatus::kRecordReady && probe.record.has_value())
  {
    const NmeaSentence& sentence = *probe.record;
    if (IsRecognizedNmeaSentence(sentence))
    {
      EndNoiseSpan(in_noise_span);
      const auto item = MakeNmeaItem(
          sentence,
          start_offset,
          result.summary.total_items_found + 1u);
      AccumulateItem(item, include_items, result);
      next_offset = start_offset + sentence.raw_bytes.size();
      return true;
    }

    ++result.summary.malformed_events;
    AddNoiseBytes(sentence.raw_bytes.size(), result.summary, in_noise_span);
    next_offset = start_offset + sentence.raw_bytes.size();
    return true;
  }

  if (probe.status == ParserStatus::kTruncated)
  {
    ++result.summary.malformed_events;
    ++result.summary.truncated_items;
    EndNoiseSpan(in_noise_span);
    next_offset = bytes.size();
    stop_scan = true;
    return true;
  }

  if (probe.status == ParserStatus::kInvalidData || probe.status == ParserStatus::kOverflow)
  {
    ++result.summary.malformed_events;
  }

  AddNoiseBytes(1u, result.summary, in_noise_span);
  next_offset = start_offset + 1u;
  return true;
}

bool ConsumeUnicoreAtOffset(const std::vector<std::uint8_t>& bytes,
                            const std::size_t start_offset,
                            const bool include_items,
                            GnssStreamInspectionResult& result,
                            std::size_t& next_offset,
                            bool& stop_scan,
                            bool& in_noise_span)
{
  UnicoreFrameFramer framer;
  const ProbeResult<UnicoreFrame> probe =
      ProbeAtOffset<UnicoreFrameFramer, UnicoreFrame>(framer, bytes, start_offset);

  if (probe.status == ParserStatus::kRecordReady && probe.record.has_value())
  {
    EndNoiseSpan(in_noise_span);
    const auto item = MakeUnicoreItem(
        *probe.record,
        start_offset,
        result.summary.total_items_found + 1u);
    AccumulateItem(item, include_items, result);
    next_offset = start_offset + probe.record->raw_bytes.size();
    return true;
  }

  if (probe.status == ParserStatus::kTruncated)
  {
    ++result.summary.malformed_events;
    ++result.summary.truncated_items;
    EndNoiseSpan(in_noise_span);
    next_offset = bytes.size();
    stop_scan = true;
    return true;
  }

  if (probe.status == ParserStatus::kInvalidData || probe.status == ParserStatus::kOverflow)
  {
    ++result.summary.malformed_events;
  }

  AddNoiseBytes(1u, result.summary, in_noise_span);
  next_offset = start_offset + 1u;
  return true;
}

bool ConsumeUbxAtOffset(const std::vector<std::uint8_t>& bytes,
                        const std::size_t start_offset,
                        const bool include_items,
                        GnssStreamInspectionResult& result,
                        std::size_t& next_offset,
                        bool& stop_scan,
                        bool& in_noise_span)
{
  UbxFrameFramer framer;
  const ProbeResult<UbxFrame> probe =
      ProbeAtOffset<UbxFrameFramer, UbxFrame>(framer, bytes, start_offset);

  if (probe.status == ParserStatus::kRecordReady && probe.record.has_value())
  {
    EndNoiseSpan(in_noise_span);
    const auto item = MakeUbxItem(
        *probe.record,
        start_offset,
        result.summary.total_items_found + 1u);
    AccumulateItem(item, include_items, result);
    next_offset = start_offset + probe.record->raw_bytes.size();
    return true;
  }

  if (probe.status == ParserStatus::kTruncated)
  {
    ++result.summary.malformed_events;
    ++result.summary.truncated_items;
    EndNoiseSpan(in_noise_span);
    next_offset = bytes.size();
    stop_scan = true;
    return true;
  }

  if (probe.status == ParserStatus::kInvalidData || probe.status == ParserStatus::kOverflow)
  {
    ++result.summary.malformed_events;
  }

  AddNoiseBytes(1u, result.summary, in_noise_span);
  next_offset = start_offset + 1u;
  return true;
}

bool ConsumeRtcmAtOffset(const std::vector<std::uint8_t>& bytes,
                         const std::size_t start_offset,
                         const bool include_items,
                         GnssStreamInspectionResult& result,
                         std::size_t& next_offset,
                         bool& stop_scan,
                         bool& in_noise_span)
{
  RtcmFrameFramer framer;
  const ProbeResult<RtcmFrame> probe =
      ProbeAtOffset<RtcmFrameFramer, RtcmFrame>(framer, bytes, start_offset);

  if (probe.status == ParserStatus::kRecordReady && probe.record.has_value())
  {
    EndNoiseSpan(in_noise_span);
    const auto item = MakeRtcmItem(
        *probe.record,
        start_offset,
        result.summary.total_items_found + 1u);
    AccumulateItem(item, include_items, result);
    next_offset = start_offset + probe.record->raw_bytes.size();
    return true;
  }

  if (probe.status == ParserStatus::kTruncated)
  {
    ++result.summary.malformed_events;
    ++result.summary.truncated_items;
    EndNoiseSpan(in_noise_span);
    next_offset = bytes.size();
    stop_scan = true;
    return true;
  }

  if (probe.status == ParserStatus::kInvalidData || probe.status == ParserStatus::kOverflow)
  {
    ++result.summary.malformed_events;
  }

  AddNoiseBytes(1u, result.summary, in_noise_span);
  next_offset = start_offset + 1u;
  return true;
}

void AppendJsonFieldSeparator(std::ostringstream& output, bool& first_field)
{
  if (!first_field)
  {
    output << ',';
  }
  first_field = false;
}

std::string EscapeJsonString(const std::string& text)
{
  std::ostringstream output;
  for (const unsigned char ch : text)
  {
    switch (ch)
    {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (ch < 0x20u)
        {
          output << "\\u"
                 << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(ch)
                 << std::dec << std::setfill(' ');
        }
        else
        {
          output << static_cast<char>(ch);
        }
        break;
    }
  }
  return output.str();
}

std::vector<std::uint8_t> ReadAllBytes(std::istream& input)
{
  std::vector<std::uint8_t> bytes;
  std::array<char, 4096> buffer{};
  while (input.good())
  {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize bytes_read = input.gcount();
    if (bytes_read <= 0)
    {
      continue;
    }

    const auto begin = reinterpret_cast<const std::uint8_t*>(buffer.data());
    bytes.insert(bytes.end(), begin, begin + bytes_read);
  }
  return bytes;
}

}  // namespace

GnssStreamInspectionResult InspectGnssStreamBytes(const std::vector<std::uint8_t>& bytes,
                                                  const bool include_items)
{
  GnssStreamInspectionResult result;
  result.summary.total_bytes_read = bytes.size();

  bool stop_scan = false;
  bool in_noise_span = false;

  for (std::size_t offset = 0; offset < bytes.size() && !stop_scan;)
  {
    std::size_t next_offset = offset + 1u;
    const std::uint8_t byte = bytes[offset];

    if (byte == '$' || byte == '!')
    {
      ConsumeNmeaAtOffset(
          bytes,
          offset,
          include_items,
          result,
          next_offset,
          stop_scan,
          in_noise_span);
      offset = next_offset;
      continue;
    }

    if (byte == 0xB5u)
    {
      ConsumeUbxAtOffset(
          bytes,
          offset,
          include_items,
          result,
          next_offset,
          stop_scan,
          in_noise_span);
      offset = next_offset;
      continue;
    }

    if (byte == '#' || byte == '%')
    {
      ConsumeUnicoreAtOffset(
          bytes,
          offset,
          include_items,
          result,
          next_offset,
          stop_scan,
          in_noise_span);
      offset = next_offset;
      continue;
    }

    if (byte == 0xD3u)
    {
      ConsumeRtcmAtOffset(
          bytes,
          offset,
          include_items,
          result,
          next_offset,
          stop_scan,
          in_noise_span);
      offset = next_offset;
      continue;
    }

    AddNoiseBytes(1u, result.summary, in_noise_span);
    ++offset;
  }

  return result;
}

GnssStreamInspectionResult InspectGnssStreamStream(std::istream& input, const bool include_items)
{
  return InspectGnssStreamBytes(ReadAllBytes(input), include_items);
}

const char* DescribeProtocolType(const ProtocolType protocol)
{
  switch (protocol)
  {
    case ProtocolType::kNmea:
      return "nmea";
    case ProtocolType::kUbx:
      return "ubx";
    case ProtocolType::kRtcm3:
      return "rtcm3";
    case ProtocolType::kUnicore:
      return "unicore";
    case ProtocolType::kUnknown:
    default:
      return "unknown";
  }
}

std::string FormatUbxMessageKey(const std::uint8_t class_id, const std::uint8_t message_id)
{
  std::ostringstream output;
  output << std::hex
         << std::uppercase
         << std::setw(2) << std::setfill('0') << static_cast<int>(class_id)
         << ':'
         << std::setw(2) << std::setfill('0') << static_cast<int>(message_id);
  return output.str();
}

std::string DescribeUbxMessage(const std::uint8_t class_id, const std::uint8_t message_id)
{
  if (class_id == 0x01u && message_id == 0x03u)
  {
    return "NAV-STATUS";
  }
  if (class_id == 0x01u && message_id == 0x07u)
  {
    return "NAV-PVT";
  }
  if (class_id == 0x01u && message_id == 0x35u)
  {
    return "NAV-SAT";
  }
  if (class_id == 0x0Au && message_id == 0x38u)
  {
    return "MON-RF";
  }
  return "unknown";
}

std::string FormatGnssStreamInspectionText(const GnssStreamInspectionResult& result,
                                           const bool summary_only)
{
  std::ostringstream output;

  if (!summary_only)
  {
    for (const auto& item : result.items)
    {
      output << item.item_index
             << " offset=" << item.byte_offset
             << " proto=" << DescribeProtocolType(item.protocol)
             << " len=" << item.length_bytes;

      if (item.protocol == ProtocolType::kNmea)
      {
        output << " id=" << item.identity;
      }
      else if (item.protocol == ProtocolType::kUbx)
      {
        output << " id=" << item.identity
               << " name=" << item.ubx_message_name;
      }
      else if (item.protocol == ProtocolType::kUnicore)
      {
        output << " name=" << item.identity;
      }
      else if (item.protocol == ProtocolType::kRtcm3)
      {
        output << " type=" << item.rtcm_message_type
               << " class=" << item.classification;
      }

      output << " crc=" << DescribeChecksumStatus(item.checksum_status) << '\n';
    }
  }

  output << "summary"
         << " total_bytes=" << result.summary.total_bytes_read
         << " items=" << result.summary.total_items_found
         << " valid=" << result.summary.valid_items
         << " invalid=" << result.summary.invalid_items
         << " malformed=" << result.summary.malformed_events
         << " truncated=" << result.summary.truncated_items
         << " noise_bytes=" << result.summary.noise_bytes
         << " noise_spans=" << result.summary.noise_spans
         << '\n';

  if (!result.summary.counts_by_protocol.empty())
  {
    output << "protocols";
    for (const auto& entry : result.summary.counts_by_protocol)
    {
      output << ' ' << entry.first << '=' << entry.second;
    }
    output << '\n';
  }

  if (!result.summary.counts_by_nmea_sentence_type.empty())
  {
    output << "nmea_types";
    for (const auto& entry : result.summary.counts_by_nmea_sentence_type)
    {
      output << ' ' << entry.first << '=' << entry.second;
    }
    output << '\n';
  }

  if (!result.summary.counts_by_ubx_message.empty())
  {
    output << "ubx_messages";
    for (const auto& entry : result.summary.counts_by_ubx_message)
    {
      output << ' ' << entry.first << '=' << entry.second;
    }
    output << '\n';
  }

  if (!result.summary.counts_by_unicore_message.empty())
  {
    output << "unicore_messages";
    for (const auto& entry : result.summary.counts_by_unicore_message)
    {
      output << ' ' << entry.first << '=' << entry.second;
    }
    output << '\n';
  }

  if (!result.summary.counts_by_rtcm_message_type.empty())
  {
    output << "rtcm_types";
    for (const auto& entry : result.summary.counts_by_rtcm_message_type)
    {
      output << ' ' << entry.first << '=' << entry.second;
    }
    output << '\n';
  }

  if (!result.summary.rtcm_msm_counts_by_constellation.empty())
  {
    output << "rtcm_msm_constellations";
    for (const auto& entry : result.summary.rtcm_msm_counts_by_constellation)
    {
      output << ' ' << DescribeRtcmConstellation(entry.first) << '=' << entry.second;
    }
    output << '\n';
  }

  return output.str();
}

std::string FormatGnssStreamInspectionJson(const GnssStreamInspectionResult& result,
                                           const bool summary_only)
{
  std::ostringstream output;
  output << '{';

  bool first_root_field = true;
  if (!summary_only)
  {
    AppendJsonFieldSeparator(output, first_root_field);
    output << "\"items\":[";
    for (std::size_t index = 0; index < result.items.size(); ++index)
    {
      if (index != 0u)
      {
        output << ',';
      }

      const auto& item = result.items[index];
      output << '{'
             << "\"index\":" << item.item_index << ','
             << "\"byte_offset\":" << item.byte_offset << ','
             << "\"protocol\":\"" << DescribeProtocolType(item.protocol) << "\","
             << "\"length_bytes\":" << item.length_bytes << ','
             << "\"checksum_status\":\"" << DescribeChecksumStatus(item.checksum_status) << "\","
             << "\"identity\":\"" << EscapeJsonString(item.identity) << "\"";

      if (item.protocol == ProtocolType::kNmea)
      {
        output << ",\"nmea_talker\":\"" << EscapeJsonString(item.nmea_talker) << "\""
               << ",\"nmea_sentence_type\":\"" << EscapeJsonString(item.nmea_sentence_type)
               << "\"";
      }
      else if (item.protocol == ProtocolType::kUbx)
      {
        output << ",\"ubx_class_id\":" << static_cast<unsigned int>(item.ubx_class_id)
               << ",\"ubx_message_id\":" << static_cast<unsigned int>(item.ubx_message_id)
               << ",\"ubx_name\":\"" << EscapeJsonString(item.ubx_message_name) << "\"";
      }
      else if (item.protocol == ProtocolType::kUnicore)
      {
        output << ",\"unicore_message\":\"" << EscapeJsonString(item.identity) << "\"";
      }
      else if (item.protocol == ProtocolType::kRtcm3)
      {
        output << ",\"message_type\":" << item.rtcm_message_type
               << ",\"classification\":\"" << EscapeJsonString(item.classification) << "\"";
      }

      output << '}';
    }
    output << ']';
  }

  AppendJsonFieldSeparator(output, first_root_field);
  output << "\"summary\":{";

  bool first_summary_field = true;
  auto write_summary_number = [&](const char* name, const std::size_t value) {
    AppendJsonFieldSeparator(output, first_summary_field);
    output << '"' << name << "\":" << value;
  };

  write_summary_number("total_bytes_read", result.summary.total_bytes_read);
  write_summary_number("total_items_found", result.summary.total_items_found);
  write_summary_number("valid_items", result.summary.valid_items);
  write_summary_number("invalid_items", result.summary.invalid_items);
  write_summary_number("malformed_events", result.summary.malformed_events);
  write_summary_number("truncated_items", result.summary.truncated_items);
  write_summary_number("noise_bytes", result.summary.noise_bytes);
  write_summary_number("noise_spans", result.summary.noise_spans);

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"counts_by_protocol\":{";
  bool first_protocol_entry = true;
  for (const auto& entry : result.summary.counts_by_protocol)
  {
    AppendJsonFieldSeparator(output, first_protocol_entry);
    output << '"' << EscapeJsonString(entry.first) << "\":" << entry.second;
  }
  output << '}';

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"counts_by_nmea_sentence_type\":{";
  bool first_nmea_entry = true;
  for (const auto& entry : result.summary.counts_by_nmea_sentence_type)
  {
    AppendJsonFieldSeparator(output, first_nmea_entry);
    output << '"' << EscapeJsonString(entry.first) << "\":" << entry.second;
  }
  output << '}';

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"counts_by_ubx_message\":{";
  bool first_ubx_entry = true;
  for (const auto& entry : result.summary.counts_by_ubx_message)
  {
    AppendJsonFieldSeparator(output, first_ubx_entry);
    output << '"' << EscapeJsonString(entry.first) << "\":" << entry.second;
  }
  output << '}';

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"counts_by_unicore_message\":{";
  bool first_unicore_entry = true;
  for (const auto& entry : result.summary.counts_by_unicore_message)
  {
    AppendJsonFieldSeparator(output, first_unicore_entry);
    output << '"' << EscapeJsonString(entry.first) << "\":" << entry.second;
  }
  output << '}';

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"counts_by_rtcm_message_type\":{";
  bool first_rtcm_entry = true;
  for (const auto& entry : result.summary.counts_by_rtcm_message_type)
  {
    AppendJsonFieldSeparator(output, first_rtcm_entry);
    output << '"' << entry.first << "\":" << entry.second;
  }
  output << '}';

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"rtcm_msm_counts_by_constellation\":{";
  bool first_msm_entry = true;
  for (const auto& entry : result.summary.rtcm_msm_counts_by_constellation)
  {
    AppendJsonFieldSeparator(output, first_msm_entry);
    output << '"' << DescribeRtcmConstellation(entry.first) << "\":" << entry.second;
  }
  output << '}';

  output << '}';
  output << '}';
  return output.str();
}

}  // namespace universal_gnss_tools
