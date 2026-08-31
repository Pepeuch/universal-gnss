#include "universal_gnss_tools/receiver_discovery_format.hpp"

#include <sstream>

namespace universal_gnss_tools
{

namespace
{

std::string EscapeJson(const std::string& input)
{
  std::string escaped;
  escaped.reserve(input.size());
  for (const char ch : input)
  {
    switch (ch)
    {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

void AppendEvidenceText(std::ostringstream& output,
                        const universal_gnss_driver::ReceiverProbeEvidence& evidence)
{
  output << "evidence=ubx:" << evidence.ubx_frames_seen
         << " unicore_ascii:" << evidence.unicore_ascii_seen
         << " unicore_binary:" << evidence.unicore_binary_seen
         << " nmea:" << evidence.nmea_sentences_seen
         << " rtcm:" << evidence.rtcm_frames_seen
         << " mavlink:" << evidence.mavlink_heartbeats_seen
         << " random_ascii:" << evidence.random_ascii_bytes_seen
         << " bytes:" << evidence.bytes_read;
}

}  // namespace

std::string FormatReceiverDiscoveryText(
    const std::vector<universal_gnss_driver::ReceiverProbeResult>& results)
{
  std::ostringstream output;
  if (results.empty())
  {
    output << "No receiver candidates found\n";
    return output.str();
  }

  for (const auto& result : results)
  {
    output << result.path
           << " baud="
           << (result.selected_baud.has_value() ? std::to_string(*result.selected_baud) : "n/a")
           << " family=" << universal_gnss_driver::ToString(result.detected_family)
           << " confidence=" << universal_gnss_driver::ToString(result.confidence)
           << " score=" << result.discovery_score
           << " source=" << universal_gnss_driver::ToString(result.source)
           << " transport=" << universal_gnss_driver::ToString(result.transport_type);
    if (result.stable_id.has_value())
    {
      output << " stable_id=" << *result.stable_id;
    }
    if (result.identity.receiver_identity.has_value())
    {
      output << " receiver_identity=" << *result.identity.receiver_identity;
    }
    if (result.identity.model.has_value())
    {
      output << " model=" << *result.identity.model;
    }
    if (result.identity.firmware_version.has_value())
    {
      output << " firmware=" << *result.identity.firmware_version;
    }
    output << ' ';
    AppendEvidenceText(output, result.evidence);
    if (!result.note.empty())
    {
      output << " note=" << result.note;
    }
    if (!result.reason.empty())
    {
      output << " reason=" << result.reason;
    }
    output << '\n';
  }

  return output.str();
}

std::string FormatReceiverDiscoveryJson(
    const std::vector<universal_gnss_driver::ReceiverProbeResult>& results)
{
  std::ostringstream output;
  output << "[\n";
  for (std::size_t index = 0u; index < results.size(); ++index)
  {
    const auto& result = results[index];
    output << "  {\n"
           << "    \"path\": \"" << EscapeJson(result.path) << "\",\n"
           << "    \"stable_id\": ";
    if (result.stable_id.has_value())
    {
      output << '"' << EscapeJson(*result.stable_id) << '"';
    }
    else
    {
      output << "null";
    }
    output << ",\n"
           << "    \"receiver_identity\": ";
    if (result.identity.receiver_identity.has_value())
    {
      output << '"' << EscapeJson(*result.identity.receiver_identity) << '"';
    }
    else
    {
      output << "null";
    }
    output << ",\n"
           << "    \"receiver_model\": ";
    if (result.identity.model.has_value())
    {
      output << '"' << EscapeJson(*result.identity.model) << '"';
    }
    else
    {
      output << "null";
    }
    output << ",\n"
           << "    \"receiver_firmware_version\": ";
    if (result.identity.firmware_version.has_value())
    {
      output << '"' << EscapeJson(*result.identity.firmware_version) << '"';
    }
    else
    {
      output << "null";
    }
    output << ",\n"
           << "    \"transport\": \""
           << universal_gnss_driver::ToString(result.transport_type) << "\",\n"
           << "    \"source\": \"" << universal_gnss_driver::ToString(result.source) << "\",\n"
           << "    \"selected_baud\": ";
    if (result.selected_baud.has_value())
    {
      output << *result.selected_baud;
    }
    else
    {
      output << "null";
    }
    output << ",\n"
           << "    \"detected_family\": \""
           << universal_gnss_driver::ToString(result.detected_family) << "\",\n"
           << "    \"confidence\": \""
           << universal_gnss_driver::ToString(result.confidence) << "\",\n"
           << "    \"score\": " << result.discovery_score << ",\n"
           << "    \"evidence\": {\n"
           << "      \"ubx_frames_seen\": " << result.evidence.ubx_frames_seen << ",\n"
           << "      \"unicore_ascii_seen\": " << result.evidence.unicore_ascii_seen << ",\n"
           << "      \"unicore_binary_seen\": " << result.evidence.unicore_binary_seen << ",\n"
           << "      \"nmea_sentences_seen\": " << result.evidence.nmea_sentences_seen << ",\n"
           << "      \"rtcm_frames_seen\": " << result.evidence.rtcm_frames_seen << ",\n"
           << "      \"mavlink_heartbeats_seen\": "
           << result.evidence.mavlink_heartbeats_seen << ",\n"
           << "      \"random_ascii_bytes_seen\": "
           << result.evidence.random_ascii_bytes_seen << ",\n"
           << "      \"bytes_read\": " << result.evidence.bytes_read << "\n"
           << "    },\n"
           << "    \"note\": \"" << EscapeJson(result.note) << "\",\n"
           << "    \"reason\": \"" << EscapeJson(result.reason) << "\"\n"
           << "  }";
    if (index + 1u != results.size())
    {
      output << ',';
    }
    output << '\n';
  }
  output << "]\n";
  return output.str();
}

}  // namespace universal_gnss_tools
