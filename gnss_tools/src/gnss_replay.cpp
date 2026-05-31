#include "universal_gnss_tools/gnss_replay.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <optional>
#include <ostream>
#include <sstream>
#include <utility>
#include <vector>

#include "universal_gnss/gnss_runtime_aggregator.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"
#include "universal_gnss_protocols/nmea_parser.hpp"
#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/unicore_framer.hpp"
#include "universal_gnss_protocols/unicore_parser.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/ubx_parser.hpp"
#include "universal_gnss_tools/gnss_stream_inspector.hpp"
#include "universal_gnss_tools/rtcm_inspector.hpp"

namespace universal_gnss_tools
{

namespace
{

using universal_gnss::GnssCapability;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRuntimeAggregator;
using universal_gnss::GnssRuntimeState;
using universal_gnss::GnssRtkMode;
using universal_gnss::HasValueAvailable;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::NmeaSentence;
using universal_gnss_protocols::NmeaSentenceFramer;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::ProtocolType;
using universal_gnss_protocols::UnicoreFrame;
using universal_gnss_protocols::UnicoreFrameFramer;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;

template <typename RecordT>
struct ProbeResult
{
  ParserStatus status{ParserStatus::kIdle};
  std::optional<RecordT> record{};
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
    };
  }

  auto finalize_result = framer.Finalize();
  return ProbeResult<RecordT>{
      finalize_result.status,
      std::move(finalize_result.record),
  };
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

void AppendJsonFieldSeparator(std::ostringstream& output, bool& first_field)
{
  if (!first_field)
  {
    output << ',';
  }
  first_field = false;
}

const char* DescribeFixType(const GnssFixType fix_type)
{
  switch (fix_type)
  {
    case GnssFixType::kNoFix:
      return "no_fix";
    case GnssFixType::kFix:
      return "fix";
    case GnssFixType::kRtkFloat:
      return "rtk_float";
    case GnssFixType::kRtkFixed:
      return "rtk_fixed";
    case GnssFixType::kDeadReckoning:
      return "dead_reckoning";
    case GnssFixType::kUnknown:
    default:
      return "unknown";
  }
}

const char* DescribeRtkMode(const std::optional<GnssRtkMode>& rtk_mode)
{
  if (!rtk_mode.has_value())
  {
    return "unknown";
  }

  switch (*rtk_mode)
  {
    case GnssRtkMode::kNone:
      return "none";
    case GnssRtkMode::kFloat:
      return "float";
    case GnssRtkMode::kFixed:
      return "fixed";
    case GnssRtkMode::kUnknown:
    default:
      return "unknown";
  }
}

std::string FormatFloat(const float value, const int precision = 2)
{
  std::ostringstream output;
  output << std::fixed << std::setprecision(precision) << value;
  return output.str();
}

std::string FormatDouble(const double value, const int precision = 6)
{
  std::ostringstream output;
  output << std::fixed << std::setprecision(precision) << value;
  return output.str();
}

std::string BuildStateTextSummary(const GnssRuntimeState& state)
{
  std::ostringstream output;
  output << "fix=" << DescribeFixType(state.fix_type)
         << " rtk=" << DescribeRtkMode(state.rtk_mode);

  if (state.latitude_deg.has_value() && state.longitude_deg.has_value())
  {
    output << " lat=" << FormatDouble(*state.latitude_deg)
           << " lon=" << FormatDouble(*state.longitude_deg);
  }
  if (state.altitude_m.has_value())
  {
    output << " alt=" << FormatDouble(*state.altitude_m, 2);
  }
  if (state.horizontal_accuracy_m.has_value())
  {
    output << " h_acc=" << FormatFloat(*state.horizontal_accuracy_m, 2);
  }
  if (state.vertical_accuracy_m.has_value())
  {
    output << " v_acc=" << FormatFloat(*state.vertical_accuracy_m, 2);
  }
  if (state.satellites_used.has_value())
  {
    output << " used=" << *state.satellites_used;
  }
  if (state.satellites_tracked.has_value())
  {
    output << " tracked=" << *state.satellites_tracked;
  }
  if (state.satellites_visible.has_value())
  {
    output << " visible=" << *state.satellites_visible;
  }
  if (state.mean_cn0_db_hz.has_value())
  {
    output << " mean_cn0=" << FormatFloat(*state.mean_cn0_db_hz, 1);
  }
  if (state.max_cn0_db_hz.has_value())
  {
    output << " max_cn0=" << FormatFloat(*state.max_cn0_db_hz, 1);
  }
  if (state.correction_age_s.has_value())
  {
    output << " corr_age=" << FormatFloat(*state.correction_age_s, 2);
  }
  if (state.interference_detected.has_value())
  {
    output << " interference=" << (*state.interference_detected ? "true" : "false");
  }
  if (state.jamming_detected.has_value())
  {
    output << " jamming=" << (*state.jamming_detected ? "true" : "false");
  }

  return output.str();
}

void WriteRuntimeStateJson(std::ostringstream& output, const GnssRuntimeState& state)
{
  output << '{';
  bool first_field = true;

  auto write_bool = [&](const char* name, const bool value) {
    AppendJsonFieldSeparator(output, first_field);
    output << '"' << name << "\":" << (value ? "true" : "false");
  };
  auto write_number = [&](const char* name, const auto value) {
    AppendJsonFieldSeparator(output, first_field);
    output << '"' << name << "\":" << value;
  };
  auto write_string = [&](const char* name, const std::string& value) {
    AppendJsonFieldSeparator(output, first_field);
    output << '"' << name << "\":\"" << EscapeJsonString(value) << '"';
  };

  write_bool("fix_valid", state.fix_valid);
  write_string("fix_type", DescribeFixType(state.fix_type));
  write_string("rtk_mode", DescribeRtkMode(state.rtk_mode));
  write_number("capability_flags", state.capability_flags);
  write_number("value_flags", state.value_flags);

  if (state.timestamp_ns.has_value())
  {
    write_number("timestamp_ns", *state.timestamp_ns);
  }
  if (state.latitude_deg.has_value())
  {
    write_number("latitude_deg", *state.latitude_deg);
  }
  if (state.longitude_deg.has_value())
  {
    write_number("longitude_deg", *state.longitude_deg);
  }
  if (state.altitude_m.has_value())
  {
    write_number("altitude_m", *state.altitude_m);
  }
  if (state.horizontal_accuracy_m.has_value())
  {
    write_number("horizontal_accuracy_m", *state.horizontal_accuracy_m);
  }
  if (state.vertical_accuracy_m.has_value())
  {
    write_number("vertical_accuracy_m", *state.vertical_accuracy_m);
  }
  if (state.hdop.has_value())
  {
    write_number("hdop", *state.hdop);
  }
  if (state.vdop.has_value())
  {
    write_number("vdop", *state.vdop);
  }
  if (state.satellites_used.has_value())
  {
    write_number("satellites_used", *state.satellites_used);
  }
  if (state.satellites_tracked.has_value())
  {
    write_number("satellites_tracked", *state.satellites_tracked);
  }
  if (state.satellites_visible.has_value())
  {
    write_number("satellites_visible", *state.satellites_visible);
  }
  if (state.mean_cn0_db_hz.has_value())
  {
    write_number("mean_cn0_db_hz", *state.mean_cn0_db_hz);
  }
  if (state.max_cn0_db_hz.has_value())
  {
    write_number("max_cn0_db_hz", *state.max_cn0_db_hz);
  }
  if (state.correction_age_s.has_value())
  {
    write_number("correction_age_s", *state.correction_age_s);
  }
  if (state.heading_deg.has_value())
  {
    write_number("heading_deg", *state.heading_deg);
  }
  if (state.dual_antenna_heading.has_value())
  {
    write_bool("dual_antenna_heading", *state.dual_antenna_heading);
  }
  if (state.interference_detected.has_value())
  {
    write_bool("interference_detected", *state.interference_detected);
  }
  if (state.jamming_detected.has_value())
  {
    write_bool("jamming_detected", *state.jamming_detected);
  }

  output << '}';
}

GnssReplaySummary BuildReplaySummary(const GnssStreamInspectionSummary& inspection_summary)
{
  GnssReplaySummary summary;
  summary.total_bytes_read = inspection_summary.total_bytes_read;
  summary.recognized_records = inspection_summary.total_items_found;
  summary.valid_records = inspection_summary.valid_items;
  summary.invalid_records = inspection_summary.invalid_items;
  summary.malformed_events = inspection_summary.malformed_events;
  summary.truncated_records = inspection_summary.truncated_items;
  summary.noise_bytes = inspection_summary.noise_bytes;
  summary.noise_spans = inspection_summary.noise_spans;
  summary.counts_by_protocol = inspection_summary.counts_by_protocol;
  summary.counts_by_nmea_sentence_type = inspection_summary.counts_by_nmea_sentence_type;
  summary.counts_by_ubx_message = inspection_summary.counts_by_ubx_message;
  summary.counts_by_unicore_message = inspection_summary.counts_by_unicore_message;
  summary.counts_by_rtcm_message_type = inspection_summary.counts_by_rtcm_message_type;
  return summary;
}

std::optional<GnssRuntimeState> BuildRuntimeUpdateFromNmea(const std::vector<std::uint8_t>& bytes,
                                                           const std::size_t byte_offset)
{
  NmeaSentenceFramer framer;
  const auto probe = ProbeAtOffset<NmeaSentenceFramer, NmeaSentence>(framer, bytes, byte_offset);
  if (probe.status != ParserStatus::kRecordReady || !probe.record.has_value())
  {
    return std::nullopt;
  }

  const NmeaSentence& sentence = *probe.record;
  if (const auto gga = universal_gnss_protocols::ParseNmeaGga(sentence);
      gga.status == ParserStatus::kRecordReady && gga.record.has_value())
  {
    return universal_gnss_protocols::NmeaGgaToRuntimeState(*gga.record);
  }
  if (const auto rmc = universal_gnss_protocols::ParseNmeaRmc(sentence);
      rmc.status == ParserStatus::kRecordReady && rmc.record.has_value())
  {
    return universal_gnss_protocols::NmeaRmcToRuntimeState(*rmc.record);
  }
  if (const auto gsa = universal_gnss_protocols::ParseNmeaGsa(sentence);
      gsa.status == ParserStatus::kRecordReady && gsa.record.has_value())
  {
    return universal_gnss_protocols::NmeaGsaToRuntimeState(*gsa.record);
  }
  if (const auto gsv = universal_gnss_protocols::ParseNmeaGsv(sentence);
      gsv.status == ParserStatus::kRecordReady && gsv.record.has_value())
  {
    GnssRuntimeState update;
    universal_gnss_protocols::MergeNmeaGsvIntoRuntimeState(*gsv.record, update);
    return update;
  }
  if (const auto gst = universal_gnss_protocols::ParseNmeaGst(sentence);
      gst.status == ParserStatus::kRecordReady && gst.record.has_value())
  {
    return universal_gnss_protocols::NmeaGstToRuntimeState(*gst.record);
  }

  return std::nullopt;
}

std::optional<GnssRuntimeState> BuildRuntimeUpdateFromUbx(const std::vector<std::uint8_t>& bytes,
                                                          const std::size_t byte_offset)
{
  UbxFrameFramer framer;
  const auto probe = ProbeAtOffset<UbxFrameFramer, UbxFrame>(framer, bytes, byte_offset);
  if (probe.status != ParserStatus::kRecordReady || !probe.record.has_value())
  {
    return std::nullopt;
  }

  const UbxFrame& frame = *probe.record;
  if (const auto nav_status = universal_gnss_protocols::ParseUbxNavStatus(frame);
      nav_status.status == ParserStatus::kRecordReady && nav_status.record.has_value())
  {
    return universal_gnss_protocols::UbxNavStatusToRuntimeState(*nav_status.record);
  }
  if (const auto nav_pvt = universal_gnss_protocols::ParseUbxNavPvt(frame);
      nav_pvt.status == ParserStatus::kRecordReady && nav_pvt.record.has_value())
  {
    return universal_gnss_protocols::UbxNavPvtToRuntimeState(*nav_pvt.record);
  }
  if (const auto nav_dop = universal_gnss_protocols::ParseUbxNavDop(frame);
      nav_dop.status == ParserStatus::kRecordReady && nav_dop.record.has_value())
  {
    return universal_gnss_protocols::UbxNavDopToRuntimeState(*nav_dop.record);
  }
  if (const auto nav_sat = universal_gnss_protocols::ParseUbxNavSat(frame);
      nav_sat.status == ParserStatus::kRecordReady && nav_sat.record.has_value())
  {
    return universal_gnss_protocols::UbxNavSatToRuntimeState(*nav_sat.record);
  }
  if (const auto mon_rf = universal_gnss_protocols::ParseUbxMonRf(frame);
      mon_rf.status == ParserStatus::kRecordReady && mon_rf.record.has_value())
  {
    return universal_gnss_protocols::UbxMonRfToRuntimeState(*mon_rf.record);
  }

  return std::nullopt;
}

std::optional<GnssRuntimeState> BuildRuntimeUpdateFromUnicore(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t byte_offset)
{
  UnicoreFrameFramer framer;
  const auto probe = ProbeAtOffset<UnicoreFrameFramer, UnicoreFrame>(framer, bytes, byte_offset);
  if (probe.status != ParserStatus::kRecordReady || !probe.record.has_value())
  {
    return std::nullopt;
  }

  const UnicoreFrame& frame = *probe.record;
  if (const auto pvtsln = universal_gnss_protocols::ParseUnicorePvtsln(frame);
      pvtsln.status == ParserStatus::kRecordReady && pvtsln.record.has_value())
  {
    return universal_gnss_protocols::UnicorePvtslnToRuntimeState(*pvtsln.record);
  }
  if (const auto bestnav = universal_gnss_protocols::ParseUnicoreBestNav(frame);
      bestnav.status == ParserStatus::kRecordReady && bestnav.record.has_value())
  {
    return universal_gnss_protocols::UnicoreBestNavToRuntimeState(*bestnav.record);
  }
  if (const auto rtkstatus = universal_gnss_protocols::ParseUnicoreRtkStatus(frame);
      rtkstatus.status == ParserStatus::kRecordReady && rtkstatus.record.has_value())
  {
    return universal_gnss_protocols::UnicoreRtkStatusToRuntimeState(*rtkstatus.record);
  }
  if (const auto rtcmstatus = universal_gnss_protocols::ParseUnicoreRtcmStatus(frame);
      rtcmstatus.status == ParserStatus::kRecordReady && rtcmstatus.record.has_value())
  {
    return universal_gnss_protocols::UnicoreRtcmStatusToRuntimeState(*rtcmstatus.record);
  }
  if (const auto satsinfo = universal_gnss_protocols::ParseUnicoreSatsInfo(frame);
      satsinfo.status == ParserStatus::kRecordReady && satsinfo.record.has_value())
  {
    return universal_gnss_protocols::UnicoreSatsInfoToRuntimeState(*satsinfo.record);
  }
  if (const auto jamstatus = universal_gnss_protocols::ParseUnicoreJamStatus(frame);
      jamstatus.status == ParserStatus::kRecordReady && jamstatus.record.has_value())
  {
    return universal_gnss_protocols::UnicoreJamStatusToRuntimeState(*jamstatus.record);
  }
  if (const auto freqjamstatus = universal_gnss_protocols::ParseUnicoreFreqJamStatus(frame);
      freqjamstatus.status == ParserStatus::kRecordReady &&
      freqjamstatus.record.has_value())
  {
    return universal_gnss_protocols::UnicoreFreqJamStatusToRuntimeState(*freqjamstatus.record);
  }

  return std::nullopt;
}

std::optional<GnssRuntimeState> BuildRuntimeUpdate(const GnssStreamInspectionItem& item,
                                                   const std::vector<std::uint8_t>& bytes)
{
  switch (item.protocol)
  {
    case ProtocolType::kNmea:
      return BuildRuntimeUpdateFromNmea(bytes, item.byte_offset);
    case ProtocolType::kUbx:
      return BuildRuntimeUpdateFromUbx(bytes, item.byte_offset);
    case ProtocolType::kUnicore:
      return BuildRuntimeUpdateFromUnicore(bytes, item.byte_offset);
    case ProtocolType::kRtcm3:
    case ProtocolType::kUnknown:
    default:
      return std::nullopt;
  }
}

}  // namespace

GnssReplayResult ReplayGnssBytes(const std::vector<std::uint8_t>& bytes, const bool include_events)
{
  const GnssStreamInspectionResult inspection = InspectGnssStreamBytes(bytes, true);

  GnssReplayResult result;
  result.summary = BuildReplaySummary(inspection.summary);

  GnssRuntimeAggregator aggregator;
  for (const auto& item : inspection.items)
  {
    GnssReplayEvent event;
    event.event_index = item.item_index;
    event.byte_offset = item.byte_offset;
    event.length_bytes = item.length_bytes;
    event.protocol = item.protocol;
    event.checksum_status = item.checksum_status;
    event.identity = item.identity;
    event.classification = item.classification;

    if (const auto update = BuildRuntimeUpdate(item, bytes); update.has_value())
    {
      event.produced_runtime_update = aggregator.Merge(*update);
      if (event.produced_runtime_update)
      {
        ++result.summary.runtime_updates;
      }
    }

    event.state_after_event = aggregator.state();
    if (include_events)
    {
      result.events.push_back(std::move(event));
    }
  }

  result.final_state = aggregator.state();
  return result;
}

GnssReplayResult ReplayGnssStream(std::istream& input, const bool include_events)
{
  return ReplayGnssBytes(ReadAllBytes(input), include_events);
}

std::string FormatGnssReplayText(const GnssReplayResult& result, const bool summary_only)
{
  std::ostringstream output;

  if (!summary_only)
  {
    for (const auto& event : result.events)
    {
      output << event.event_index
             << " offset=" << event.byte_offset
             << " proto=" << DescribeProtocolType(event.protocol)
             << " len=" << event.length_bytes;

      if (!event.identity.empty())
      {
        if (event.protocol == ProtocolType::kRtcm3)
        {
          output << " type=" << event.identity;
        }
        else if (event.protocol == ProtocolType::kUbx)
        {
          output << " id=" << event.identity;
          if (!event.classification.empty())
          {
            output << " name=" << event.classification;
          }
        }
        else
        {
          output << " id=" << event.identity;
        }
      }

      output << " update=" << (event.produced_runtime_update ? "yes" : "no")
             << " crc=" << DescribeChecksumStatus(event.checksum_status)
             << ' ' << BuildStateTextSummary(event.state_after_event)
             << '\n';
    }
  }

  output << "summary"
         << " total_bytes=" << result.summary.total_bytes_read
         << " records=" << result.summary.recognized_records
         << " valid=" << result.summary.valid_records
         << " invalid=" << result.summary.invalid_records
         << " malformed=" << result.summary.malformed_events
         << " truncated=" << result.summary.truncated_records
         << " noise_bytes=" << result.summary.noise_bytes
         << " noise_spans=" << result.summary.noise_spans
         << " runtime_updates=" << result.summary.runtime_updates
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

  output << "final_state " << BuildStateTextSummary(result.final_state) << '\n';
  return output.str();
}

std::string FormatGnssReplayJson(const GnssReplayResult& result, const bool summary_only)
{
  std::ostringstream output;
  output << '{';

  bool first_root_field = true;
  if (!summary_only)
  {
    AppendJsonFieldSeparator(output, first_root_field);
    output << "\"events\":[";
    for (std::size_t index = 0; index < result.events.size(); ++index)
    {
      if (index != 0u)
      {
        output << ',';
      }

      const auto& event = result.events[index];
      output << '{';
      bool first_event_field = true;
      auto write_event_number = [&](const char* name, const auto value) {
        AppendJsonFieldSeparator(output, first_event_field);
        output << '"' << name << "\":" << value;
      };
      auto write_event_string = [&](const char* name, const std::string& value) {
        AppendJsonFieldSeparator(output, first_event_field);
        output << '"' << name << "\":\"" << EscapeJsonString(value) << '"';
      };

      write_event_number("index", event.event_index);
      write_event_number("byte_offset", event.byte_offset);
      write_event_number("length_bytes", event.length_bytes);
      write_event_string("protocol", DescribeProtocolType(event.protocol));
      write_event_string("checksum_status", DescribeChecksumStatus(event.checksum_status));
      write_event_string("identity", event.identity);
      write_event_string("classification", event.classification);
      AppendJsonFieldSeparator(output, first_event_field);
      output << "\"runtime_updated\":" << (event.produced_runtime_update ? "true" : "false");
      AppendJsonFieldSeparator(output, first_event_field);
      output << "\"state_after_event\":";
      WriteRuntimeStateJson(output, event.state_after_event);
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
  write_summary_number("recognized_records", result.summary.recognized_records);
  write_summary_number("valid_records", result.summary.valid_records);
  write_summary_number("invalid_records", result.summary.invalid_records);
  write_summary_number("malformed_events", result.summary.malformed_events);
  write_summary_number("truncated_records", result.summary.truncated_records);
  write_summary_number("noise_bytes", result.summary.noise_bytes);
  write_summary_number("noise_spans", result.summary.noise_spans);
  write_summary_number("runtime_updates", result.summary.runtime_updates);

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
  output << '}';

  AppendJsonFieldSeparator(output, first_root_field);
  output << "\"final_state\":";
  WriteRuntimeStateJson(output, result.final_state);

  output << '}';
  return output.str();
}

}  // namespace universal_gnss_tools
