#include "universal_gnss_tools/rtcm_inspector.hpp"

#include <array>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>

#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"

namespace universal_gnss_tools
{

namespace
{

using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::ParserStatus;
using universal_gnss_protocols::RtcmConstellation;
using universal_gnss_protocols::RtcmCorrectionMonitor;
using universal_gnss_protocols::RtcmFrame;
using universal_gnss_protocols::RtcmFrameFramer;
using universal_gnss_protocols::RtcmMessageInfo;

RtcmMessageInfo BuildMessageInfo(const RtcmFrame& frame)
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

void AccumulateFrame(const RtcmFrame& frame,
                     const RtcmMessageInfo& message_info,
                     const std::size_t byte_offset,
                     const bool include_frames,
                     RtcmCorrectionMonitor& correction_monitor,
                     RtcmInspectionResult& result)
{
  ++result.summary.total_frames_found;
  if (frame.checksum_status == ChecksumStatus::kValid)
  {
    ++result.summary.valid_frames;
  }
  else
  {
    ++result.summary.invalid_frames;
  }

  ++result.summary.counts_by_message_type[message_info.message_type];
  if (message_info.is_msm && message_info.msm_constellation != RtcmConstellation::kUnknown)
  {
    ++result.summary.msm_counts_by_constellation[message_info.msm_constellation];
  }
  correction_monitor.ObserveFrame(frame);

  if (!include_frames)
  {
    return;
  }

  RtcmFrameInspection inspection;
  inspection.frame_index = result.summary.total_frames_found;
  inspection.byte_offset = byte_offset;
  inspection.length_bytes = frame.raw_bytes.size();
  inspection.message_type = message_info.message_type;
  inspection.checksum_status = frame.checksum_status;
  inspection.message_info = message_info;
  result.frames.push_back(inspection);
}

void ConsumeParserResult(const universal_gnss_protocols::ParserResult<RtcmFrame>& parser_result,
                         const std::size_t total_bytes_read,
                         const bool include_frames,
                         RtcmCorrectionMonitor& correction_monitor,
                         RtcmInspectionResult& result)
{
  switch (parser_result.status)
  {
    case ParserStatus::kRecordReady:
      if (parser_result.record.has_value())
      {
        const RtcmFrame& frame = *parser_result.record;
        const RtcmMessageInfo message_info = BuildMessageInfo(frame);
        const std::size_t byte_offset = total_bytes_read - frame.raw_bytes.size();
        AccumulateFrame(
            frame, message_info, byte_offset, include_frames, correction_monitor, result);
      }
      break;
    case ParserStatus::kInvalidData:
    case ParserStatus::kOverflow:
      ++result.summary.malformed_events;
      break;
    case ParserStatus::kTruncated:
      ++result.summary.malformed_events;
      ++result.summary.truncated_frames;
      break;
    case ParserStatus::kIdle:
    case ParserStatus::kNeedMoreData:
    case ParserStatus::kSkipped:
      break;
  }
}

template <typename ByteProvider>
RtcmInspectionResult InspectWithProvider(ByteProvider&& provider, const bool include_frames)
{
  RtcmInspectionResult result;
  RtcmFrameFramer framer;
  RtcmCorrectionMonitor correction_monitor;

  provider([&](const std::uint8_t byte) {
    ++result.summary.total_bytes_read;
    const auto parser_result = framer.PushByte(byte);
    ConsumeParserResult(
        parser_result, result.summary.total_bytes_read, include_frames, correction_monitor, result);
  });

  const auto finalize_result = framer.Finalize();
  ConsumeParserResult(
      finalize_result, result.summary.total_bytes_read, include_frames, correction_monitor, result);
  result.summary.last_base_station_arp = correction_monitor.last_base_station_arp();
  result.summary.semantic_observations =
      universal_gnss_protocols::BuildRtcmSemanticObservations(correction_monitor);
  return result;
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

void WriteRtcmSemanticFieldJson(std::ostream& output,
                                const universal_gnss_protocols::RtcmSemanticField& field,
                                bool& first_field)
{
  if (!first_field)
  {
    output << ',';
  }
  first_field = false;
  output << '"' << field.key << "\":\"" << field.value << '"';
}

}  // namespace

RtcmInspectionResult InspectRtcmBytes(const std::vector<std::uint8_t>& bytes,
                                      const bool include_frames)
{
  return InspectWithProvider(
      [&](const auto& consume_byte) {
        for (const auto byte : bytes)
        {
          consume_byte(byte);
        }
      },
      include_frames);
}

RtcmInspectionResult InspectRtcmStream(std::istream& input, const bool include_frames)
{
  return InspectWithProvider(
      [&](const auto& consume_byte) {
        std::array<char, 4096> buffer{};
        while (input.good())
        {
          input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
          const std::streamsize bytes_read = input.gcount();
          for (std::streamsize i = 0; i < bytes_read; ++i)
          {
            consume_byte(static_cast<std::uint8_t>(
                static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)])));
          }
        }
      },
      include_frames);
}

std::string DescribeRtcmConstellation(const RtcmConstellation constellation)
{
  switch (constellation)
  {
    case RtcmConstellation::kGps:
      return "gps";
    case RtcmConstellation::kGlonass:
      return "glonass";
    case RtcmConstellation::kGalileo:
      return "galileo";
    case RtcmConstellation::kSbas:
      return "sbas";
    case RtcmConstellation::kQzss:
      return "qzss";
    case RtcmConstellation::kBeiDou:
      return "beidou";
    case RtcmConstellation::kNavIc:
      return "navic";
    case RtcmConstellation::kUnknown:
    default:
      return "unknown";
  }
}

std::string DescribeRtcmMessageInfo(const RtcmMessageInfo& message_info)
{
  if (message_info.is_station_arp)
  {
    return "station_arp";
  }

  if (message_info.is_glonass_bias)
  {
    return "glonass_bias";
  }

  if (message_info.is_msm)
  {
    return "msm:" + DescribeRtcmConstellation(message_info.msm_constellation);
  }

  return "unknown";
}

std::string DescribeChecksumStatus(const ChecksumStatus status)
{
  switch (status)
  {
    case ChecksumStatus::kValid:
      return "valid";
    case ChecksumStatus::kInvalid:
      return "invalid";
    case ChecksumStatus::kMissing:
      return "missing";
    case ChecksumStatus::kNotChecked:
    default:
      return "not_checked";
  }
}

std::string FormatRtcmSemanticObservationText(
    const universal_gnss_protocols::RtcmSemanticObservation& observation)
{
  std::ostringstream output;
  output << observation.name
         << " seen=" << (observation.seen ? "true" : "false")
         << " decoded=" << (observation.decoded ? "true" : "false")
         << " valid=" << (observation.valid ? "true" : "false")
         << " decode_success=" << observation.decode_success_count
         << " decode_failure=" << observation.decode_failure_count
         << " malformed=" << observation.malformed_count;
  if (observation.message_type != 0u)
  {
    output << " message_type=" << observation.message_type;
  }
  if (observation.last_seen_timestamp_ns.has_value())
  {
    output << " last_seen_ns=" << *observation.last_seen_timestamp_ns;
  }
  if (observation.last_decoded_timestamp_ns.has_value())
  {
    output << " last_decoded_ns=" << *observation.last_decoded_timestamp_ns;
  }
  if (observation.age_ns.has_value())
  {
    output << " age_ns=" << *observation.age_ns;
  }
  for (const auto& field : observation.fields)
  {
    output << ' ' << field.key << '=' << field.value;
  }
  return output.str();
}

void WriteRtcmSemanticObservationsJson(
    std::ostream& output,
    const universal_gnss_protocols::RtcmSemanticObservations& observations)
{
  output << '[';
  for (std::size_t index = 0u; index < observations.size(); ++index)
  {
    if (index != 0u)
    {
      output << ',';
    }

    const auto& observation = observations[index];
    output << '{'
           << "\"name\":\"" << observation.name << "\","
           << "\"message_type\":" << observation.message_type << ','
           << "\"seen\":" << (observation.seen ? "true" : "false") << ','
           << "\"decoded\":" << (observation.decoded ? "true" : "false") << ','
           << "\"valid\":" << (observation.valid ? "true" : "false") << ','
           << "\"decode_success_count\":" << observation.decode_success_count << ','
           << "\"decode_failure_count\":" << observation.decode_failure_count << ','
           << "\"malformed_count\":" << observation.malformed_count << ','
           << "\"last_seen_timestamp_ns\":";
    if (observation.last_seen_timestamp_ns.has_value())
    {
      output << *observation.last_seen_timestamp_ns;
    }
    else
    {
      output << "null";
    }
    output << ','
           << "\"last_decoded_timestamp_ns\":";
    if (observation.last_decoded_timestamp_ns.has_value())
    {
      output << *observation.last_decoded_timestamp_ns;
    }
    else
    {
      output << "null";
    }
    output << ','
           << "\"age_ns\":";
    if (observation.age_ns.has_value())
    {
      output << *observation.age_ns;
    }
    else
    {
      output << "null";
    }
    output << ",\"fields\":{";
    bool first_field = true;
    for (const auto& field : observation.fields)
    {
      WriteRtcmSemanticFieldJson(output, field, first_field);
    }
    output << "}}";
  }
  output << ']';
}

std::string FormatRtcmInspectionText(const RtcmInspectionResult& result, const bool summary_only)
{
  std::ostringstream output;

  if (!summary_only)
  {
    for (const auto& frame : result.frames)
    {
      output << frame.frame_index
             << " offset=" << frame.byte_offset
             << " len=" << frame.length_bytes
             << " type=" << frame.message_type
             << " class=" << DescribeRtcmMessageInfo(frame.message_info)
             << " crc=" << DescribeChecksumStatus(frame.checksum_status) << '\n';
    }
  }

  output << "summary"
         << " total_bytes=" << result.summary.total_bytes_read
         << " frames=" << result.summary.total_frames_found
         << " valid=" << result.summary.valid_frames
         << " invalid=" << result.summary.invalid_frames
         << " malformed=" << result.summary.malformed_events
         << " truncated=" << result.summary.truncated_frames
         << '\n';

  if (!result.summary.counts_by_message_type.empty())
  {
    output << "message_types";
    for (const auto& entry : result.summary.counts_by_message_type)
    {
      output << ' ' << entry.first << '=' << entry.second;
    }
    output << '\n';
  }

  if (!result.summary.msm_counts_by_constellation.empty())
  {
    output << "msm_constellations";
    for (const auto& entry : result.summary.msm_counts_by_constellation)
    {
      output << ' ' << DescribeRtcmConstellation(entry.first) << '=' << entry.second;
    }
    output << '\n';
  }

  if (result.summary.semantic_observations.empty())
  {
    output << "semantic_observations unavailable\n";
  }
  else
  {
    for (const auto& observation : result.summary.semantic_observations)
    {
      output << FormatRtcmSemanticObservationText(observation) << '\n';
    }
  }

  return output.str();
}

std::string FormatRtcmInspectionJson(const RtcmInspectionResult& result, const bool summary_only)
{
  std::ostringstream output;
  output << '{';

  bool first_root_field = true;
  if (!summary_only)
  {
    AppendJsonFieldSeparator(output, first_root_field);
    output << "\"frames\":[";
    for (std::size_t i = 0; i < result.frames.size(); ++i)
    {
      if (i != 0u)
      {
        output << ',';
      }
      const auto& frame = result.frames[i];
      output << '{'
             << "\"index\":" << frame.frame_index << ','
             << "\"byte_offset\":" << frame.byte_offset << ','
             << "\"length_bytes\":" << frame.length_bytes << ','
             << "\"message_type\":" << frame.message_type << ','
             << "\"classification\":\"" << DescribeRtcmMessageInfo(frame.message_info) << "\","
             << "\"crc_status\":\"" << DescribeChecksumStatus(frame.checksum_status) << "\""
             << '}';
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
  write_summary_number("total_frames_found", result.summary.total_frames_found);
  write_summary_number("valid_frames", result.summary.valid_frames);
  write_summary_number("invalid_frames", result.summary.invalid_frames);
  write_summary_number("malformed_events", result.summary.malformed_events);
  write_summary_number("truncated_frames", result.summary.truncated_frames);

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"counts_by_message_type\":{";
  bool first_count = true;
  for (const auto& entry : result.summary.counts_by_message_type)
  {
    if (!first_count)
    {
      output << ',';
    }
    first_count = false;
    output << '"' << entry.first << "\":" << entry.second;
  }
  output << '}';

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"msm_counts_by_constellation\":{";
  bool first_constellation = true;
  for (const auto& entry : result.summary.msm_counts_by_constellation)
  {
    if (!first_constellation)
    {
      output << ',';
    }
    first_constellation = false;
    output << '"' << DescribeRtcmConstellation(entry.first) << "\":" << entry.second;
  }
  output << '}';

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"base_station_arp\":";
  WriteBaseStationArpJson(output, result.summary.last_base_station_arp);

  AppendJsonFieldSeparator(output, first_summary_field);
  output << "\"semantic_observations\":";
  WriteRtcmSemanticObservationsJson(output, result.summary.semantic_observations);

  output << "}}";
  return output.str();
}

}  // namespace universal_gnss_tools
