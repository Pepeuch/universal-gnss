#include "universal_gnss_tools/ntrip_monitor.hpp"

#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

#include "universal_gnss_tools/rtcm_inspector.hpp"

namespace universal_gnss_tools
{

namespace
{

std::string EscapeJsonString(const std::string_view text)
{
  std::ostringstream stream;
  for (const char ch : text)
  {
    switch (ch)
    {
      case '\"':
        stream << "\\\"";
        break;
      case '\\':
        stream << "\\\\";
        break;
      case '\b':
        stream << "\\b";
        break;
      case '\f':
        stream << "\\f";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20u)
        {
          stream << "\\u"
                 << std::hex
                 << std::setw(4)
                 << std::setfill('0')
                 << static_cast<int>(static_cast<unsigned char>(ch))
                 << std::dec
                 << std::setfill(' ');
        }
        else
        {
          stream << ch;
        }
        break;
    }
  }

  return stream.str();
}

std::string ExtractResponseStatusLine(const std::string_view header)
{
  if (header.empty())
  {
    return {};
  }

  const std::size_t crlf = header.find("\r\n");
  if (crlf != std::string_view::npos)
  {
    return std::string(header.substr(0u, crlf));
  }

  const std::size_t lf = header.find('\n');
  if (lf != std::string_view::npos)
  {
    return std::string(header.substr(0u, lf));
  }

  return std::string(header);
}

void AppendJsonFieldSeparator(std::ostringstream& stream, bool& first_field)
{
  if (!first_field)
  {
    stream << ',';
  }
  first_field = false;
}

}  // namespace

bool NtripMonitorValidationResult::ok() const
{
  return error == NtripMonitorValidationError::kNone;
}

NtripMonitorValidationResult ValidateNtripMonitorOptions(
    const NtripMonitorOptions& options)
{
  if (options.host.empty())
  {
    return {NtripMonitorValidationError::kMissingHost, "--host is required"};
  }
  if (options.port == 0u)
  {
    return {NtripMonitorValidationError::kMissingPort, "--port is required"};
  }
  if (options.mountpoint.empty())
  {
    return {NtripMonitorValidationError::kMissingMountpoint, "--mountpoint is required"};
  }
  if (options.latitude_deg.has_value() && !options.longitude_deg.has_value())
  {
    return {NtripMonitorValidationError::kMissingLongitude,
            "--lon is required when --lat is provided"};
  }
  if (options.longitude_deg.has_value() && !options.latitude_deg.has_value())
  {
    return {NtripMonitorValidationError::kMissingLatitude,
            "--lat is required when --lon is provided"};
  }
  if (options.gga_interval_s.has_value() && *options.gga_interval_s == 0u)
  {
    return {NtripMonitorValidationError::kInvalidGgaInterval,
            "--gga-interval must be greater than zero"};
  }
  if (options.max_bytes.has_value() && *options.max_bytes == 0u)
  {
    return {NtripMonitorValidationError::kInvalidMaxBytes,
            "--max-bytes must be greater than zero"};
  }
  if (options.max_seconds.has_value() && *options.max_seconds == 0u)
  {
    return {NtripMonitorValidationError::kInvalidMaxSeconds,
            "--max-seconds must be greater than zero"};
  }
  if (options.gga_interval_s.has_value() &&
      (!options.latitude_deg.has_value() || !options.longitude_deg.has_value()))
  {
    return {NtripMonitorValidationError::kGgaIntervalRequiresPosition,
            "--gga-interval requires --lat and --lon"};
  }

  return {};
}

universal_gnss_ntrip::NtripConfig BuildNtripMonitorConfig(
    const NtripMonitorOptions& options)
{
  universal_gnss_ntrip::NtripConfig config;
  config.host = options.host;
  config.port = options.port;
  config.mountpoint = options.mountpoint;
  config.username = options.username;
  config.password = options.password;
  config.user_agent =
      options.user_agent.empty() ? universal_gnss_ntrip::kDefaultNtripUserAgent
                                 : options.user_agent;
  config.send_gga = options.gga_interval_s.has_value();
  if (options.gga_interval_s.has_value())
  {
    config.gga_interval_s = *options.gga_interval_s;
  }
  return config;
}

std::optional<universal_gnss::GnssRuntimeState> BuildNtripMonitorRuntimeState(
    const NtripMonitorOptions& options)
{
  if (!options.latitude_deg.has_value() || !options.longitude_deg.has_value())
  {
    return std::nullopt;
  }

  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = options.latitude_deg;
  state.longitude_deg = options.longitude_deg;
  state.altitude_m = options.altitude_m;
  return state;
}

NtripMonitorSnapshot BuildNtripMonitorSnapshot(
    const NtripMonitorOptions& options,
    const std::string& client_state,
    const universal_gnss_ntrip::NtripConnectionMetrics& metrics,
    const universal_gnss_protocols::RtcmCorrectionMonitor& correction_monitor,
    universal_gnss::GnssHealthSummary correction_health,
    const NtripMonitorStopReason stop_reason,
    const std::optional<std::int64_t> elapsed_time_ns,
    std::string response_header)
{
  NtripMonitorSnapshot snapshot;
  snapshot.options = options;
  snapshot.client_state = client_state;
  snapshot.response_header = std::move(response_header);
  snapshot.bytes_received = metrics.bytes_received;
  snapshot.bytes_sent = metrics.bytes_sent;
  snapshot.rtcm_frames_seen = metrics.rtcm_frames_seen;
  snapshot.rtcm_frames_received = metrics.rtcm_frames_received;
  snapshot.invalid_rtcm_frames = metrics.invalid_rtcm_frames;
  snapshot.gga_sent_count = metrics.gga_sent_count;
  snapshot.gga_send_errors = metrics.gga_send_errors;
  snapshot.reconnect_count = metrics.reconnect_count;
  snapshot.request_sent = metrics.request_sent;
  snapshot.response_received = metrics.response_received;
  snapshot.base_position_seen = correction_monitor.HasSeenBasePositionMessage();
  snapshot.base_position_1005_seen = correction_monitor.HasSeenBasePosition1005();
  snapshot.base_position_1006_seen = correction_monitor.HasSeenBasePosition1006();
  snapshot.glonass_bias_1230_seen = correction_monitor.HasSeenGlonassBias1230();
  snapshot.last_rtcm_message_type = metrics.last_rtcm_message_type;
  snapshot.last_gga_sent_timestamp_ns = metrics.last_gga_sent_timestamp_ns;
  snapshot.elapsed_time_ns = elapsed_time_ns;
  snapshot.last_error = metrics.last_error;
  snapshot.correction_health = std::move(correction_health);
  snapshot.stop_reason = stop_reason;

  for (const auto& entry : correction_monitor.message_type_activity())
  {
    snapshot.message_type_counts[entry.first] = entry.second.count;
  }
  for (const auto& entry : correction_monitor.msm_constellation_activity())
  {
    snapshot.msm_constellation_counts[entry.first] = entry.second.count;
  }

  return snapshot;
}

std::string DescribeGnssDiagnosticSeverity(
    const universal_gnss::GnssDiagnosticSeverity severity)
{
  using universal_gnss::GnssDiagnosticSeverity;

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

std::string DescribeNtripClientError(
    const universal_gnss_ntrip::NtripClientError error)
{
  using universal_gnss_ntrip::NtripClientError;

  switch (error)
  {
    case NtripClientError::kNone:
      return "none";
    case NtripClientError::kConfiguration:
      return "configuration";
    case NtripClientError::kAuthentication:
      return "authentication";
    case NtripClientError::kHttp:
      return "http";
    case NtripClientError::kProtocol:
      return "protocol";
    case NtripClientError::kTimeout:
      return "timeout";
    case NtripClientError::kDisconnected:
      return "disconnected";
    case NtripClientError::kUnknown:
      return "unknown";
  }

  return "unknown";
}

std::string DescribeNtripMonitorStopReason(
    const NtripMonitorStopReason stop_reason)
{
  switch (stop_reason)
  {
    case NtripMonitorStopReason::kRunning:
      return "running";
    case NtripMonitorStopReason::kCompleted:
      return "completed";
    case NtripMonitorStopReason::kConnectFailed:
      return "connect_failed";
    case NtripMonitorStopReason::kRequestFailed:
      return "request_failed";
    case NtripMonitorStopReason::kGgaSendFailed:
      return "gga_send_failed";
    case NtripMonitorStopReason::kReadError:
      return "read_error";
    case NtripMonitorStopReason::kDisconnected:
      return "disconnected";
    case NtripMonitorStopReason::kMaxBytes:
      return "max_bytes";
    case NtripMonitorStopReason::kMaxSeconds:
      return "max_seconds";
    case NtripMonitorStopReason::kInterrupted:
      return "interrupted";
  }

  return "completed";
}

std::string FormatNtripMonitorStatusLine(const NtripMonitorSnapshot& snapshot)
{
  std::ostringstream stream;
  stream << "status"
         << " state=" << snapshot.client_state
         << " bytes_received=" << snapshot.bytes_received
         << " rtcm_frames=" << snapshot.rtcm_frames_received
         << " invalid_rtcm=" << snapshot.invalid_rtcm_frames
         << " health="
         << DescribeGnssDiagnosticSeverity(snapshot.correction_health.overall_severity)
         << " base_position_seen=" << std::boolalpha << snapshot.base_position_seen;
  if (snapshot.last_rtcm_message_type.has_value())
  {
    stream << " last_type=" << *snapshot.last_rtcm_message_type;
  }
  if (snapshot.gga_sent_count > 0u)
  {
    stream << " gga_sent=" << snapshot.gga_sent_count;
  }
  return stream.str();
}

std::string FormatNtripMonitorSummaryText(const NtripMonitorSnapshot& snapshot)
{
  std::ostringstream stream;
  stream << std::boolalpha;
  stream << "Summary:\n"
         << "  endpoint=" << snapshot.options.host
         << ':' << snapshot.options.port
         << '/' << snapshot.options.mountpoint
         << " state=" << snapshot.client_state
         << " stop_reason=" << DescribeNtripMonitorStopReason(snapshot.stop_reason) << '\n';

  stream << "  bytes_received=" << snapshot.bytes_received
         << " bytes_sent=" << snapshot.bytes_sent
         << " request_sent=" << snapshot.request_sent
         << " response_received=" << snapshot.response_received;
  if (snapshot.elapsed_time_ns.has_value())
  {
    stream << " elapsed_s=" << std::fixed << std::setprecision(3)
           << static_cast<double>(*snapshot.elapsed_time_ns) / 1000000000.0;
  }
  stream << '\n';

  stream << "  rtcm_frames_seen=" << snapshot.rtcm_frames_seen
         << " valid_frames=" << snapshot.rtcm_frames_received
         << " invalid_frames=" << snapshot.invalid_rtcm_frames
         << " gga_sent=" << snapshot.gga_sent_count
         << " gga_send_errors=" << snapshot.gga_send_errors
         << " reconnects=" << snapshot.reconnect_count;
  if (snapshot.last_rtcm_message_type.has_value())
  {
    stream << " last_type=" << *snapshot.last_rtcm_message_type;
  }
  stream << '\n';

  stream << "  base_position_seen=" << snapshot.base_position_seen
         << " base_1005_seen=" << snapshot.base_position_1005_seen
         << " base_1006_seen=" << snapshot.base_position_1006_seen
         << " glonass_bias_1230_seen=" << snapshot.glonass_bias_1230_seen << '\n';

  stream << "  correction_health="
         << DescribeGnssDiagnosticSeverity(snapshot.correction_health.overall_severity)
         << " correction_available=" << snapshot.correction_health.correction_available
         << " stale_data=" << snapshot.correction_health.stale_data
         << " last_error=" << DescribeNtripClientError(snapshot.last_error) << '\n';

  if (!snapshot.message_type_counts.empty())
  {
    stream << "message_types";
    for (const auto& entry : snapshot.message_type_counts)
    {
      stream << ' ' << entry.first << '=' << entry.second;
    }
    stream << '\n';
  }

  if (!snapshot.msm_constellation_counts.empty())
  {
    stream << "msm_constellations";
    for (const auto& entry : snapshot.msm_constellation_counts)
    {
      stream << ' ' << DescribeRtcmConstellation(entry.first) << '=' << entry.second;
    }
    stream << '\n';
  }

  const std::string status_line = ExtractResponseStatusLine(snapshot.response_header);
  if (!status_line.empty())
  {
    stream << "response_status " << status_line << '\n';
  }

  return stream.str();
}

std::string FormatNtripMonitorSummaryJson(const NtripMonitorSnapshot& snapshot)
{
  std::ostringstream stream;
  stream << "{"
         << "\"type\":\"summary\","
         << "\"host\":\"" << EscapeJsonString(snapshot.options.host) << "\","
         << "\"port\":" << snapshot.options.port << ','
         << "\"mountpoint\":\"" << EscapeJsonString(snapshot.options.mountpoint) << "\","
         << "\"client_state\":\"" << EscapeJsonString(snapshot.client_state) << "\","
         << "\"stop_reason\":\""
         << EscapeJsonString(DescribeNtripMonitorStopReason(snapshot.stop_reason)) << "\","
         << "\"bytes_received\":" << snapshot.bytes_received << ','
         << "\"bytes_sent\":" << snapshot.bytes_sent << ','
         << "\"request_sent\":" << (snapshot.request_sent ? "true" : "false") << ','
         << "\"response_received\":" << (snapshot.response_received ? "true" : "false") << ','
         << "\"rtcm_frames_seen\":" << snapshot.rtcm_frames_seen << ','
         << "\"rtcm_frames_received\":" << snapshot.rtcm_frames_received << ','
         << "\"invalid_rtcm_frames\":" << snapshot.invalid_rtcm_frames << ','
         << "\"gga_sent_count\":" << snapshot.gga_sent_count << ','
         << "\"gga_send_errors\":" << snapshot.gga_send_errors << ','
         << "\"reconnect_count\":" << snapshot.reconnect_count << ','
         << "\"base_position_seen\":" << (snapshot.base_position_seen ? "true" : "false")
         << ','
         << "\"base_position_1005_seen\":"
         << (snapshot.base_position_1005_seen ? "true" : "false") << ','
         << "\"base_position_1006_seen\":"
         << (snapshot.base_position_1006_seen ? "true" : "false") << ','
         << "\"glonass_bias_1230_seen\":"
         << (snapshot.glonass_bias_1230_seen ? "true" : "false") << ','
         << "\"last_error\":\""
         << EscapeJsonString(DescribeNtripClientError(snapshot.last_error)) << "\",";

  stream << "\"elapsed_time_ns\":";
  if (snapshot.elapsed_time_ns.has_value())
  {
    stream << *snapshot.elapsed_time_ns;
  }
  else
  {
    stream << "null";
  }
  stream << ',';

  stream << "\"last_rtcm_message_type\":";
  if (snapshot.last_rtcm_message_type.has_value())
  {
    stream << *snapshot.last_rtcm_message_type;
  }
  else
  {
    stream << "null";
  }
  stream << ',';

  stream << "\"last_gga_sent_timestamp_ns\":";
  if (snapshot.last_gga_sent_timestamp_ns.has_value())
  {
    stream << *snapshot.last_gga_sent_timestamp_ns;
  }
  else
  {
    stream << "null";
  }
  stream << ',';

  stream << "\"correction_health\":{"
         << "\"severity\":\""
         << EscapeJsonString(
                DescribeGnssDiagnosticSeverity(snapshot.correction_health.overall_severity))
         << "\","
         << "\"fix_valid\":"
         << (snapshot.correction_health.fix_valid ? "true" : "false") << ','
         << "\"rtk_available\":"
         << (snapshot.correction_health.rtk_available ? "true" : "false") << ','
         << "\"correction_available\":"
         << (snapshot.correction_health.correction_available ? "true" : "false") << ','
         << "\"receiver_healthy\":"
         << (snapshot.correction_health.receiver_healthy ? "true" : "false") << ','
         << "\"transport_healthy\":"
         << (snapshot.correction_health.transport_healthy ? "true" : "false") << ','
         << "\"parser_healthy\":"
         << (snapshot.correction_health.parser_healthy ? "true" : "false") << ','
         << "\"stale_data\":"
         << (snapshot.correction_health.stale_data ? "true" : "false")
         << "},";

  stream << "\"message_type_counts\":{";
  bool first_field = true;
  for (const auto& entry : snapshot.message_type_counts)
  {
    AppendJsonFieldSeparator(stream, first_field);
    stream << '"' << entry.first << "\":" << entry.second;
  }
  stream << "},";

  stream << "\"msm_constellation_counts\":{";
  first_field = true;
  for (const auto& entry : snapshot.msm_constellation_counts)
  {
    AppendJsonFieldSeparator(stream, first_field);
    stream << '"'
           << EscapeJsonString(DescribeRtcmConstellation(entry.first))
           << "\":" << entry.second;
  }
  stream << "},";

  stream << "\"response_status_line\":";
  const std::string status_line = ExtractResponseStatusLine(snapshot.response_header);
  if (!status_line.empty())
  {
    stream << '"' << EscapeJsonString(status_line) << '"';
  }
  else
  {
    stream << "null";
  }

  stream << "}\n";
  return stream.str();
}

}  // namespace universal_gnss_tools
