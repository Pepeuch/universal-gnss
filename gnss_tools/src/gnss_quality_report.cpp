#include "universal_gnss_tools/gnss_quality_report.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <iterator>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_protocols/unicore_framer.hpp"
#include "universal_gnss_protocols/unicore_parser.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/ubx_parser.hpp"
#include "universal_gnss_tools/gnss_replay.hpp"
#include "universal_gnss_tools/gnss_stream_inspector.hpp"
#include "universal_gnss_tools/rtcm_inspector.hpp"
#include "universal_gnss_tools/runtime_state_format.hpp"

namespace universal_gnss_tools
{

namespace
{

using universal_gnss::GnssDiagnosticCategory;
using universal_gnss::GnssDiagnosticEvent;
using universal_gnss::GnssDiagnosticEvents;
using universal_gnss::GnssDiagnosticSeverity;
using universal_gnss::GnssFixType;
using universal_gnss::GnssRuntimeState;
using universal_gnss::GnssRtkMode;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::ProtocolType;
using universal_gnss_protocols::RtcmCorrectionMonitor;
using universal_gnss_protocols::RtcmConstellation;
using universal_gnss_protocols::RtcmFrame;
using universal_gnss_protocols::RtcmFrameFramer;
using universal_gnss_protocols::UnicoreFrame;
using universal_gnss_protocols::UnicoreFrameFramer;
using universal_gnss_protocols::UbxFrame;
using universal_gnss_protocols::UbxFrameFramer;
using universal_gnss_protocols::UbxRxmRtcmMessageUse;

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
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>());
}

const char* DescribeFixType(const GnssFixType fix_type)
{
  switch (fix_type)
  {
    case GnssFixType::kUnknown:
      return "unknown";
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
  }

  return "unknown";
}

const char* DescribeRtkMode(const std::optional<GnssRtkMode>& rtk_mode)
{
  if (!rtk_mode.has_value())
  {
    return "unknown";
  }

  switch (*rtk_mode)
  {
    case GnssRtkMode::kUnknown:
      return "unknown";
    case GnssRtkMode::kNone:
      return "none";
    case GnssRtkMode::kFloat:
      return "float";
    case GnssRtkMode::kFixed:
      return "fixed";
  }

  return "unknown";
}

const char* DescribeDiagnosticSeverity(const GnssDiagnosticSeverity severity)
{
  switch (severity)
  {
    case GnssDiagnosticSeverity::kOk:
      return "ok";
    case GnssDiagnosticSeverity::kInfo:
      return "info";
    case GnssDiagnosticSeverity::kWarning:
      return "warning";
    case GnssDiagnosticSeverity::kError:
      return "error";
    case GnssDiagnosticSeverity::kStale:
      return "stale";
    case GnssDiagnosticSeverity::kUnknown:
      return "unknown";
  }

  return "unknown";
}

const char* DescribeDiagnosticCategory(const GnssDiagnosticCategory category)
{
  switch (category)
  {
    case GnssDiagnosticCategory::kRuntime:
      return "runtime";
    case GnssDiagnosticCategory::kParser:
      return "parser";
    case GnssDiagnosticCategory::kTransport:
      return "transport";
    case GnssDiagnosticCategory::kCorrection:
      return "correction";
    case GnssDiagnosticCategory::kReceiver:
      return "receiver";
    case GnssDiagnosticCategory::kConfiguration:
      return "configuration";
    case GnssDiagnosticCategory::kTiming:
      return "timing";
  }

  return "runtime";
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

void WriteBaseStationArpJson(
    std::ostringstream& output,
    const std::optional<universal_gnss_protocols::RtcmBaseStationArpRecord>& arp_record)
{
  if (!arp_record.has_value())
  {
    output << "null";
    return;
  }

  output << '{'
         << "\"message_type\":" << arp_record->message_type << ','
         << "\"station_id\":" << arp_record->station_id << ','
         << "\"itrf_year\":" << static_cast<unsigned int>(arp_record->itrf_year) << ','
         << "\"gps_indicator\":" << (arp_record->gps_indicator ? "true" : "false") << ','
         << "\"glonass_indicator\":" << (arp_record->glonass_indicator ? "true" : "false") << ','
         << "\"galileo_indicator\":" << (arp_record->galileo_indicator ? "true" : "false")
         << ','
         << "\"reference_station_indicator\":"
         << (arp_record->reference_station_indicator ? "true" : "false") << ','
         << "\"ecef_x_m\":" << arp_record->ecef_x_m << ','
         << "\"ecef_y_m\":" << arp_record->ecef_y_m << ','
         << "\"ecef_z_m\":" << arp_record->ecef_z_m << ','
         << "\"single_receiver_oscillator_indicator\":"
         << (arp_record->single_receiver_oscillator_indicator ? "true" : "false") << ','
         << "\"quarter_cycle_indicator\":"
         << static_cast<unsigned int>(arp_record->quarter_cycle_indicator) << ','
         << "\"antenna_height_m\":";
  if (arp_record->antenna_height_m.has_value())
  {
    output << *arp_record->antenna_height_m;
  }
  else
  {
    output << "null";
  }
  output << '}';
}

std::optional<universal_gnss_protocols::UbxRxmRtcmRecord> ParseRxmRtcmAtOffset(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t byte_offset)
{
  UbxFrameFramer framer;
  const auto probe = ProbeAtOffset<UbxFrameFramer, UbxFrame>(framer, bytes, byte_offset);
  if (probe.status != ParserStatus::kRecordReady || !probe.record.has_value())
  {
    return std::nullopt;
  }

  const auto parsed = universal_gnss_protocols::ParseUbxRxmRtcm(*probe.record);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    return std::nullopt;
  }

  return parsed.record;
}

std::optional<universal_gnss_protocols::UbxMonHwRecord> ParseMonHwAtOffset(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t byte_offset)
{
  UbxFrameFramer framer;
  const auto probe = ProbeAtOffset<UbxFrameFramer, UbxFrame>(framer, bytes, byte_offset);
  if (probe.status != ParserStatus::kRecordReady || !probe.record.has_value())
  {
    return std::nullopt;
  }

  const auto parsed = universal_gnss_protocols::ParseUbxMonHw(*probe.record);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    return std::nullopt;
  }

  return parsed.record;
}

std::optional<RtcmFrame> ParseRtcmFrameAtOffset(const std::vector<std::uint8_t>& bytes,
                                                const std::size_t byte_offset)
{
  RtcmFrameFramer framer;
  const auto probe = ProbeAtOffset<RtcmFrameFramer, RtcmFrame>(framer, bytes, byte_offset);
  if (probe.status != ParserStatus::kRecordReady || !probe.record.has_value())
  {
    return std::nullopt;
  }

  return probe.record;
}

template <typename RecordT, typename ParseFn>
std::optional<RecordT> ParseUnicoreRecordAtOffset(const std::vector<std::uint8_t>& bytes,
                                                  const std::size_t byte_offset,
                                                  ParseFn&& parse_fn)
{
  UnicoreFrameFramer framer;
  const auto probe = ProbeAtOffset<UnicoreFrameFramer, UnicoreFrame>(framer, bytes, byte_offset);
  if (probe.status != ParserStatus::kRecordReady || !probe.record.has_value())
  {
    return std::nullopt;
  }

  const auto parsed = std::forward<ParseFn>(parse_fn)(*probe.record);
  if (parsed.status != ParserStatus::kRecordReady || !parsed.record.has_value())
  {
    return std::nullopt;
  }

  return parsed.record;
}

void AddDiagnostic(GnssDiagnosticEvents& diagnostics, GnssDiagnosticEvent event)
{
  diagnostics.push_back(std::move(event));
}

void AddParserSummaryDiagnostics(const GnssStreamInspectionSummary& inspection_summary,
                                 GnssDiagnosticEvents& diagnostics)
{
  if (inspection_summary.invalid_items > 0u)
  {
    AddDiagnostic(diagnostics,
                  {GnssDiagnosticSeverity::kWarning,
                   GnssDiagnosticCategory::kParser,
                   "log.invalid_records",
                   "Checksum-invalid records were observed in the log",
                   std::nullopt,
                   std::string("gnss_quality_report")});
  }

  if (inspection_summary.malformed_events > 0u)
  {
    AddDiagnostic(diagnostics,
                  {GnssDiagnosticSeverity::kWarning,
                   GnssDiagnosticCategory::kParser,
                   "log.malformed_data",
                   "Malformed protocol data was observed in the log",
                   std::nullopt,
                   std::string("gnss_quality_report")});
  }

  if (inspection_summary.truncated_items > 0u)
  {
    AddDiagnostic(diagnostics,
                  {GnssDiagnosticSeverity::kWarning,
                   GnssDiagnosticCategory::kParser,
                   "log.truncated_records",
                   "The log ended with truncated protocol data",
                   std::nullopt,
                   std::string("gnss_quality_report")});
  }
}

void AddRtcmDiagnostics(const RtcmCorrectionMonitor& correction_monitor,
                        GnssDiagnosticEvents& diagnostics)
{
  if (correction_monitor.invalid_frames() > 0u)
  {
    AddDiagnostic(diagnostics,
                  {GnssDiagnosticSeverity::kWarning,
                   GnssDiagnosticCategory::kCorrection,
                   "rtcm.invalid_frames_detected",
                   "RTCM frames with invalid CRC or malformed payloads were observed",
                   std::nullopt,
                   std::string("rtcm_correction_monitor")});
  }

  if (correction_monitor.GlonassBias1230MalformedCount() > 0u)
  {
    AddDiagnostic(diagnostics,
                  {GnssDiagnosticSeverity::kWarning,
                   GnssDiagnosticCategory::kParser,
                   "rtcm.1230_malformed",
                   "Malformed RTCM 1230 GLONASS code-phase bias payloads were observed",
                   correction_monitor.LastGlonassBias1230TimestampNs(),
                   std::string("rtcm_correction_monitor")});
  }

  if (correction_monitor.HasDecodedGlonassBias1230() &&
      !correction_monitor.LastGlonassBias1230Valid())
  {
    AddDiagnostic(diagnostics,
                  {GnssDiagnosticSeverity::kWarning,
                   GnssDiagnosticCategory::kCorrection,
                   "rtcm.1230_not_valid",
                   "The latest RTCM 1230 GLONASS code-phase bias message is not marked valid",
                   correction_monitor.LastDecodedGlonassBias1230TimestampNs(),
                   std::string("rtcm_correction_monitor")});
  }

  if (correction_monitor.MsmMalformedCount() > 0u)
  {
    AddDiagnostic(diagnostics,
                  {GnssDiagnosticSeverity::kWarning,
                   GnssDiagnosticCategory::kParser,
                   "rtcm.msm_malformed",
                   "Malformed RTCM MSM payloads were observed",
                   correction_monitor.LastMsmTimestampNs(),
                   std::string("rtcm_correction_monitor")});
  }
}

GnssQualityLevel ClassifyQualityLevel(const GnssRuntimeState& state,
                                      const GnssDiagnosticEvents& diagnostics)
{
  if (state.fix_type == GnssFixType::kRtkFixed ||
      state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed))
  {
    return GnssQualityLevel::kRtkFixed;
  }

  if (state.fix_type == GnssFixType::kRtkFloat ||
      state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFloat))
  {
    return GnssQualityLevel::kRtkFloat;
  }

  if (!state.fix_valid)
  {
    return state.fix_type == GnssFixType::kUnknown ? GnssQualityLevel::kUnknown
                                                   : GnssQualityLevel::kPoor;
  }

  GnssQualityLevel base_level = GnssQualityLevel::kUsable;
  if (state.horizontal_accuracy_m.has_value())
  {
    if (*state.horizontal_accuracy_m <= 2.0f)
    {
      base_level = GnssQualityLevel::kGood;
    }
    else if (*state.horizontal_accuracy_m <= 10.0f)
    {
      base_level = GnssQualityLevel::kUsable;
    }
    else
    {
      base_level = GnssQualityLevel::kPoor;
    }
  }
  else if (state.hdop.has_value())
  {
    if (*state.hdop <= 1.5f)
    {
      base_level = GnssQualityLevel::kGood;
    }
    else if (*state.hdop <= 4.0f)
    {
      base_level = GnssQualityLevel::kUsable;
    }
    else
    {
      base_level = GnssQualityLevel::kPoor;
    }
  }
  else if (state.satellites_used.has_value() && *state.satellites_used < 4u)
  {
    base_level = GnssQualityLevel::kPoor;
  }

  if (universal_gnss::HasDiagnosticErrors(diagnostics))
  {
    if (base_level == GnssQualityLevel::kGood)
    {
      return GnssQualityLevel::kUsable;
    }
    if (base_level == GnssQualityLevel::kUsable)
    {
      return GnssQualityLevel::kPoor;
    }
  }

  if (universal_gnss::HasDiagnosticWarnings(diagnostics) &&
      base_level == GnssQualityLevel::kGood)
  {
    return GnssQualityLevel::kUsable;
  }

  return base_level;
}

std::size_t CountWarnings(const GnssDiagnosticEvents& diagnostics)
{
  std::size_t count = 0u;
  for (const auto& event : diagnostics)
  {
    if (event.severity == GnssDiagnosticSeverity::kWarning ||
        event.severity == GnssDiagnosticSeverity::kStale)
    {
      ++count;
    }
  }
  return count;
}

std::size_t CountErrors(const GnssDiagnosticEvents& diagnostics)
{
  std::size_t count = 0u;
  for (const auto& event : diagnostics)
  {
    if (event.severity == GnssDiagnosticSeverity::kError)
    {
      ++count;
    }
  }
  return count;
}

void PopulateBestAccuracy(const GnssReplayResult& replay_result,
                          GnssQualityReportSummary& summary)
{
  for (const auto& event : replay_result.events)
  {
    if (!event.state_after_event.horizontal_accuracy_m.has_value())
    {
      continue;
    }

    if (!summary.best_horizontal_accuracy_m.has_value() ||
        *event.state_after_event.horizontal_accuracy_m < *summary.best_horizontal_accuracy_m)
    {
      summary.best_horizontal_accuracy_m = *event.state_after_event.horizontal_accuracy_m;
    }
  }
}

void WriteDiagnosticJson(std::ostringstream& output, const GnssDiagnosticEvent& event)
{
  output << '{';
  bool first_field = true;
  auto write_string = [&](const char* name, const std::string& value) {
    AppendJsonFieldSeparator(output, first_field);
    output << '"' << name << "\":\"" << EscapeJsonString(value) << '"';
  };
  auto write_number = [&](const char* name, const auto value) {
    AppendJsonFieldSeparator(output, first_field);
    output << '"' << name << "\":" << value;
  };

  write_string("severity", DescribeDiagnosticSeverity(event.severity));
  write_string("category", DescribeDiagnosticCategory(event.category));
  write_string("code", event.code);
  write_string("message", event.message);
  if (event.timestamp_ns.has_value())
  {
    write_number("timestamp_ns", *event.timestamp_ns);
  }
  if (event.source.has_value())
  {
    write_string("source", *event.source);
  }
  output << '}';
}

}  // namespace

const char* DescribeGnssQualityLevel(const GnssQualityLevel level)
{
  switch (level)
  {
    case GnssQualityLevel::kUnknown:
      return "unknown";
    case GnssQualityLevel::kPoor:
      return "poor";
    case GnssQualityLevel::kUsable:
      return "usable";
    case GnssQualityLevel::kGood:
      return "good";
    case GnssQualityLevel::kRtkFloat:
      return "rtk_float";
    case GnssQualityLevel::kRtkFixed:
      return "rtk_fixed";
  }

  return "unknown";
}

GnssQualityReport BuildGnssQualityReportBytes(const std::vector<std::uint8_t>& bytes)
{
  const GnssStreamInspectionResult inspection = InspectGnssStreamBytes(bytes, true);
  const GnssReplayResult replay = ReplayGnssBytes(bytes, true);

  GnssQualityReport report;
  report.final_state = replay.final_state;

  report.summary.total_bytes_read = replay.summary.total_bytes_read;
  report.summary.records_processed = replay.summary.recognized_records;
  report.summary.runtime_updates = replay.summary.runtime_updates;
  report.summary.counts_by_protocol = replay.summary.counts_by_protocol;
  report.summary.final_fix_type = replay.final_state.fix_type;
  report.summary.final_rtk_mode = replay.final_state.rtk_mode;
  report.summary.latest_horizontal_accuracy_m = replay.final_state.horizontal_accuracy_m;
  report.summary.latest_vertical_accuracy_m = replay.final_state.vertical_accuracy_m;
  report.summary.latest_hdop = replay.final_state.hdop;
  report.summary.latest_vdop = replay.final_state.vdop;
  report.summary.satellites_used = replay.final_state.satellites_used;
  report.summary.satellites_tracked = replay.final_state.satellites_tracked;
  report.summary.satellites_visible = replay.final_state.satellites_visible;
  report.summary.mean_cn0_db_hz = replay.final_state.mean_cn0_db_hz;
  report.summary.max_cn0_db_hz = replay.final_state.max_cn0_db_hz;
  PopulateBestAccuracy(replay, report.summary);

  RtcmCorrectionMonitor correction_monitor;
  for (const auto& entry : inspection.summary.counts_by_rtcm_message_type)
  {
    report.rtcm.message_type_counts[entry.first] = entry.second;
  }

  for (const auto& item : inspection.items)
  {
    if (item.protocol == ProtocolType::kRtcm3)
    {
      if (const auto frame = ParseRtcmFrameAtOffset(bytes, item.byte_offset); frame.has_value())
      {
        correction_monitor.ObserveFrame(*frame);
      }
      else if (item.checksum_status == ChecksumStatus::kInvalid)
      {
        correction_monitor.ObserveInvalidFrame();
      }
      continue;
    }

    if (item.protocol == ProtocolType::kUnicore)
    {
      if (const auto jam_status =
              ParseUnicoreRecordAtOffset<universal_gnss_protocols::UnicoreJamStatusRecord>(
                  bytes,
                  item.byte_offset,
                  universal_gnss_protocols::ParseUnicoreJamStatus);
          jam_status.has_value())
      {
        report.diagnostics.push_back(
            universal_gnss_protocols::UnicoreJamStatusToDiagnosticEvent(*jam_status));
        continue;
      }

      if (const auto freq_jam_status =
              ParseUnicoreRecordAtOffset<universal_gnss_protocols::UnicoreFreqJamStatusRecord>(
                  bytes,
                  item.byte_offset,
                  universal_gnss_protocols::ParseUnicoreFreqJamStatus);
          freq_jam_status.has_value())
      {
        report.diagnostics.push_back(
            universal_gnss_protocols::UnicoreFreqJamStatusToDiagnosticEvent(*freq_jam_status));
        continue;
      }

      if (const auto hw_status =
              ParseUnicoreRecordAtOffset<universal_gnss_protocols::UnicoreHwStatusRecord>(
                  bytes,
                  item.byte_offset,
                  universal_gnss_protocols::ParseUnicoreHwStatus);
          hw_status.has_value())
      {
        report.diagnostics.push_back(
            universal_gnss_protocols::UnicoreHwStatusToDiagnosticEvent(*hw_status));
      }

      continue;
    }

    if (item.protocol != ProtocolType::kUbx)
    {
      continue;
    }

    if (item.ubx_class_id == 0x0Au && item.ubx_message_id == 0x09u)
    {
      const auto parsed_record = ParseMonHwAtOffset(bytes, item.byte_offset);
      if (!parsed_record.has_value())
      {
        continue;
      }

      const auto events = universal_gnss_protocols::UbxMonHwToDiagnosticEvents(*parsed_record);
      report.diagnostics.insert(report.diagnostics.end(), events.begin(), events.end());
      continue;
    }

    if (item.ubx_class_id != 0x02u || item.ubx_message_id != 0x32u)
    {
      continue;
    }

    const auto parsed_record = ParseRxmRtcmAtOffset(bytes, item.byte_offset);
    if (!parsed_record.has_value())
    {
      continue;
    }

    ++report.rtcm.receiver_side.events_observed;
    if (parsed_record->crc_failed)
    {
      ++report.rtcm.receiver_side.crc_failed_messages;
    }
    if (parsed_record->message_use == UbxRxmRtcmMessageUse::kUsed)
    {
      ++report.rtcm.receiver_side.accepted_messages;
    }
    else if (parsed_record->message_use == UbxRxmRtcmMessageUse::kNotUsed)
    {
      ++report.rtcm.receiver_side.not_used_messages;
    }

    report.diagnostics.push_back(
        universal_gnss_protocols::UbxRxmRtcmToDiagnosticEvent(*parsed_record));
  }

  report.rtcm.total_frames = static_cast<std::size_t>(correction_monitor.total_frames());
  report.rtcm.valid_frames = static_cast<std::size_t>(correction_monitor.valid_frames());
  report.rtcm.invalid_frames = static_cast<std::size_t>(correction_monitor.invalid_frames());
  report.rtcm.last_base_station_arp = correction_monitor.last_base_station_arp();
  report.rtcm.semantic_observations =
      universal_gnss_protocols::BuildRtcmSemanticObservations(correction_monitor);
  for (const auto& entry : correction_monitor.msm_constellation_activity())
  {
    report.rtcm.msm_constellation_counts[entry.first] =
        static_cast<std::size_t>(entry.second.count);
  }

  AddParserSummaryDiagnostics(inspection.summary, report.diagnostics);
  AddRtcmDiagnostics(correction_monitor, report.diagnostics);

  report.summary.warning_count = CountWarnings(report.diagnostics);
  report.summary.error_count = CountErrors(report.diagnostics);
  report.summary.quality_level = ClassifyQualityLevel(report.final_state, report.diagnostics);

  return report;
}

GnssQualityReport BuildGnssQualityReportStream(std::istream& input)
{
  return BuildGnssQualityReportBytes(ReadAllBytes(input));
}

std::string FormatGnssQualityReportText(const GnssQualityReport& report, const bool summary_only)
{
  std::ostringstream output;
  output << "quality level=" << DescribeGnssQualityLevel(report.summary.quality_level)
         << " fix=" << DescribeFixType(report.summary.final_fix_type)
         << " rtk=" << DescribeRtkMode(report.summary.final_rtk_mode)
         << '\n';

  output << "processing total_bytes=" << report.summary.total_bytes_read
         << " records=" << report.summary.records_processed
         << " runtime_updates=" << report.summary.runtime_updates
         << " warnings=" << report.summary.warning_count
         << " errors=" << report.summary.error_count
         << '\n';

  output << "final_state " << FormatRuntimeStateCompact(report.final_state) << '\n';

  output << "accuracy";
  if (report.summary.best_horizontal_accuracy_m.has_value())
  {
    output << " best_h_acc_m=" << std::fixed << std::setprecision(3)
           << *report.summary.best_horizontal_accuracy_m;
  }
  if (report.summary.latest_horizontal_accuracy_m.has_value())
  {
    output << " latest_h_acc_m=" << std::fixed << std::setprecision(3)
           << *report.summary.latest_horizontal_accuracy_m;
  }
  if (report.summary.latest_vertical_accuracy_m.has_value())
  {
    output << " latest_v_acc_m=" << std::fixed << std::setprecision(3)
           << *report.summary.latest_vertical_accuracy_m;
  }
  if (report.summary.latest_hdop.has_value())
  {
    output << " hdop=" << std::fixed << std::setprecision(2) << *report.summary.latest_hdop;
  }
  if (report.summary.latest_vdop.has_value())
  {
    output << " vdop=" << std::fixed << std::setprecision(2) << *report.summary.latest_vdop;
  }
  output << '\n';

  output << "rtcm total_frames=" << report.rtcm.total_frames
         << " valid_frames=" << report.rtcm.valid_frames
         << " invalid_frames=" << report.rtcm.invalid_frames
         << " receiver_events=" << report.rtcm.receiver_side.events_observed
         << " receiver_accepted=" << report.rtcm.receiver_side.accepted_messages
         << " receiver_not_used=" << report.rtcm.receiver_side.not_used_messages
         << " receiver_crc_failed=" << report.rtcm.receiver_side.crc_failed_messages
         << '\n';

  if (report.rtcm.last_base_station_arp.has_value())
  {
    output << "rtcm_base"
           << " station_id=" << report.rtcm.last_base_station_arp->station_id
           << " ecef_x_m=" << report.rtcm.last_base_station_arp->ecef_x_m
           << " ecef_y_m=" << report.rtcm.last_base_station_arp->ecef_y_m
           << " ecef_z_m=" << report.rtcm.last_base_station_arp->ecef_z_m;
    if (report.rtcm.last_base_station_arp->antenna_height_m.has_value())
    {
      output << " antenna_height_m=" << *report.rtcm.last_base_station_arp->antenna_height_m;
    }
    output << '\n';
  }

  for (const auto& observation : report.rtcm.semantic_observations)
  {
    output << "rtcm_semantic " << FormatRtcmSemanticObservationText(observation) << '\n';
  }

  if (!report.rtcm.message_type_counts.empty())
  {
    output << "rtcm_types";
    for (const auto& entry : report.rtcm.message_type_counts)
    {
      output << ' ' << entry.first << '=' << entry.second;
    }
    output << '\n';
  }

  if (!report.rtcm.msm_constellation_counts.empty())
  {
    output << "rtcm_constellations";
    for (const auto& entry : report.rtcm.msm_constellation_counts)
    {
      output << ' ' << DescribeRtcmConstellation(entry.first) << '=' << entry.second;
    }
    output << '\n';
  }

  if (!summary_only && !report.diagnostics.empty())
  {
    output << "diagnostics"
           << '\n';
    for (const auto& event : report.diagnostics)
    {
      if (event.severity == GnssDiagnosticSeverity::kOk ||
          event.severity == GnssDiagnosticSeverity::kInfo)
      {
        continue;
      }

      output << "- severity=" << DescribeDiagnosticSeverity(event.severity)
             << " category=" << DescribeDiagnosticCategory(event.category)
             << " code=" << event.code
             << " message=\"" << event.message << '"';
      if (event.source.has_value())
      {
        output << " source=" << *event.source;
      }
      output << '\n';
    }
  }

  return output.str();
}

std::string FormatGnssQualityReportJson(const GnssQualityReport& report, const bool summary_only)
{
  (void)summary_only;

  std::ostringstream output;
  output << '{';
  bool first_root_field = true;

  AppendJsonFieldSeparator(output, first_root_field);
  output << "\"summary\":{";
  bool first_summary_field = true;
  auto write_summary_number = [&](const char* name, const auto value) {
    AppendJsonFieldSeparator(output, first_summary_field);
    output << '"' << name << "\":" << value;
  };
  auto write_summary_string = [&](const char* name, const std::string& value) {
    AppendJsonFieldSeparator(output, first_summary_field);
    output << '"' << name << "\":\"" << EscapeJsonString(value) << '"';
  };
  auto write_summary_optional_number = [&](const char* name, const auto& value) {
    AppendJsonFieldSeparator(output, first_summary_field);
    output << '"' << name << "\":";
    if (value.has_value())
    {
      output << *value;
    }
    else
    {
      output << "null";
    }
  };

  write_summary_number("total_bytes_read", report.summary.total_bytes_read);
  write_summary_number("records_processed", report.summary.records_processed);
  write_summary_number("runtime_updates", report.summary.runtime_updates);
  write_summary_string("quality_level", DescribeGnssQualityLevel(report.summary.quality_level));
  write_summary_string("final_fix_type", DescribeFixType(report.summary.final_fix_type));
  write_summary_string("final_rtk_mode", DescribeRtkMode(report.summary.final_rtk_mode));
  write_summary_optional_number(
      "best_horizontal_accuracy_m", report.summary.best_horizontal_accuracy_m);
  write_summary_optional_number(
      "latest_horizontal_accuracy_m", report.summary.latest_horizontal_accuracy_m);
  write_summary_optional_number(
      "latest_vertical_accuracy_m", report.summary.latest_vertical_accuracy_m);
  write_summary_optional_number("latest_hdop", report.summary.latest_hdop);
  write_summary_optional_number("latest_vdop", report.summary.latest_vdop);
  write_summary_optional_number("satellites_used", report.summary.satellites_used);
  write_summary_optional_number("satellites_tracked", report.summary.satellites_tracked);
  write_summary_optional_number("satellites_visible", report.summary.satellites_visible);
  write_summary_optional_number("mean_cn0_db_hz", report.summary.mean_cn0_db_hz);
  write_summary_optional_number("max_cn0_db_hz", report.summary.max_cn0_db_hz);
  write_summary_number("warning_count", report.summary.warning_count);
  write_summary_number("error_count", report.summary.error_count);

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"counts_by_protocol\":{";
  bool first_protocol_field = true;
  for (const auto& entry : report.summary.counts_by_protocol)
  {
    AppendJsonFieldSeparator(output, first_protocol_field);
    output << '"' << EscapeJsonString(entry.first) << "\":" << entry.second;
  }
  output << '}';
  output << '}';

  AppendJsonFieldSeparator(output, first_root_field);
  output << "\"final_state\":" << FormatRuntimeStateJson(report.final_state);

  AppendJsonFieldSeparator(output, first_root_field);
  output << "\"rtcm\":{";
  bool first_rtcm_field = true;
  auto write_rtcm_number = [&](const char* name, const auto value) {
    AppendJsonFieldSeparator(output, first_rtcm_field);
    output << '"' << name << "\":" << value;
  };
  write_rtcm_number("total_frames", report.rtcm.total_frames);
  write_rtcm_number("valid_frames", report.rtcm.valid_frames);
  write_rtcm_number("invalid_frames", report.rtcm.invalid_frames);

  AppendJsonFieldSeparator(output, first_rtcm_field);
  output << "\"base_station_arp\":";
  WriteBaseStationArpJson(output, report.rtcm.last_base_station_arp);

  AppendJsonFieldSeparator(output, first_rtcm_field);
  output << "\"semantic_observations\":";
  WriteRtcmSemanticObservationsJson(output, report.rtcm.semantic_observations);

  AppendJsonFieldSeparator(output, first_rtcm_field);
  output << "\"message_type_counts\":{";
  bool first_message_type = true;
  for (const auto& entry : report.rtcm.message_type_counts)
  {
    AppendJsonFieldSeparator(output, first_message_type);
    output << '"' << entry.first << "\":" << entry.second;
  }
  output << '}';

  AppendJsonFieldSeparator(output, first_rtcm_field);
  output << "\"msm_constellation_counts\":{";
  bool first_constellation = true;
  for (const auto& entry : report.rtcm.msm_constellation_counts)
  {
    AppendJsonFieldSeparator(output, first_constellation);
    output << '"' << EscapeJsonString(DescribeRtcmConstellation(entry.first))
           << "\":" << entry.second;
  }
  output << '}';

  AppendJsonFieldSeparator(output, first_rtcm_field);
  output << "\"receiver_side\":{";
  bool first_receiver_field = true;
  auto write_receiver_number = [&](const char* name, const auto value) {
    AppendJsonFieldSeparator(output, first_receiver_field);
    output << '"' << name << "\":" << value;
  };
  write_receiver_number("events_observed", report.rtcm.receiver_side.events_observed);
  write_receiver_number("accepted_messages", report.rtcm.receiver_side.accepted_messages);
  write_receiver_number("not_used_messages", report.rtcm.receiver_side.not_used_messages);
  write_receiver_number("crc_failed_messages", report.rtcm.receiver_side.crc_failed_messages);
  output << '}';
  output << '}';

  AppendJsonFieldSeparator(output, first_root_field);
  output << "\"diagnostics\":[";
  for (std::size_t index = 0u; index < report.diagnostics.size(); ++index)
  {
    if (index != 0u)
    {
      output << ',';
    }
    WriteDiagnosticJson(output, report.diagnostics[index]);
  }
  output << ']';

  output << '}';
  return output.str();
}

}  // namespace universal_gnss_tools
