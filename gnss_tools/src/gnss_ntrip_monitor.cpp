#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_ntrip/ntrip_client.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include "universal_gnss_tools/ntrip_monitor.hpp"

namespace
{

using universal_gnss_ntrip::NtripClient;
using universal_gnss_ntrip::NtripClientError;
using universal_gnss_ntrip::NtripClientState;
using universal_gnss_ntrip::NtripGgaSendStatus;
using universal_gnss_protocols::RtcmCorrectionHealthOptions;
using universal_gnss_tools::BuildNtripMonitorConfig;
using universal_gnss_tools::BuildNtripMonitorRuntimeState;
using universal_gnss_tools::BuildNtripMonitorSnapshot;
using universal_gnss_tools::DescribeNtripClientError;
using universal_gnss_tools::FormatNtripMonitorStatusLine;
using universal_gnss_tools::FormatNtripMonitorSummaryJson;
using universal_gnss_tools::FormatNtripMonitorSummaryText;
using universal_gnss_tools::NtripMonitorOptions;
using universal_gnss_tools::NtripMonitorStopReason;
using universal_gnss_tools::ValidateNtripMonitorOptions;

volatile std::sig_atomic_t g_interrupted = 0;

void HandleSignal(const int)
{
  g_interrupted = 1;
}

std::int64_t MonotonicNowNs()
{
  using clock = std::chrono::steady_clock;
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             clock::now().time_since_epoch())
      .count();
}

void PrintUsage(const char* program_name)
{
  std::cout
      << "Usage: " << program_name
      << " --host <name> --port <int> --mountpoint <name>"
      << " [--user <name>] [--password <text>] [--user-agent <text>]"
      << " [--lat <deg> --lon <deg> [--alt <m>] [--gga-interval <seconds>]]"
      << " [--max-bytes <bytes>] [--max-seconds <seconds>]"
      << " [--read-timeout-ms <ms>] [--summary] [--json]\n"
      << "Examples:\n"
      << "  " << program_name
      << " --host caster.example.org --port 2101 --mountpoint MOUNT\n"
      << "  " << program_name
      << " --host caster.example.org --port 2101 --mountpoint NEAR"
      << " --user user --password pass\n"
      << "  " << program_name
      << " --host caster.example.org --port 2101 --mountpoint NEAR"
      << " --lat 48.0 --lon 2.0 --gga-interval 5\n"
      << "  " << program_name
      << " --host caster.example.org --port 2101 --mountpoint NEAR"
      << " --max-seconds 30\n";
}

bool ParseUnsigned64(const std::string& text, std::uint64_t& value)
{
  try
  {
    std::size_t consumed = 0u;
    const auto parsed = std::stoull(text, &consumed, 10);
    if (consumed != text.size())
    {
      return false;
    }
    value = parsed;
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

bool ParseUnsigned32(const std::string& text, std::uint32_t& value)
{
  std::uint64_t parsed = 0u;
  if (!ParseUnsigned64(text, parsed) || parsed > 0xFFFFFFFFull)
  {
    return false;
  }
  value = static_cast<std::uint32_t>(parsed);
  return true;
}

bool ParseUnsigned16(const std::string& text, std::uint16_t& value)
{
  std::uint64_t parsed = 0u;
  if (!ParseUnsigned64(text, parsed) || parsed > 0xFFFFull)
  {
    return false;
  }
  value = static_cast<std::uint16_t>(parsed);
  return true;
}

bool ParseSizeT(const std::string& text, std::size_t& value)
{
  std::uint64_t parsed = 0u;
  if (!ParseUnsigned64(text, parsed))
  {
    return false;
  }
  value = static_cast<std::size_t>(parsed);
  return true;
}

bool ParseDouble(const std::string& text, double& value)
{
  try
  {
    std::size_t consumed = 0u;
    const auto parsed = std::stod(text, &consumed);
    if (consumed != text.size())
    {
      return false;
    }
    value = parsed;
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

const char* DescribeClientState(const NtripClientState state)
{
  switch (state)
  {
    case NtripClientState::kDisconnected:
      return "disconnected";
    case NtripClientState::kConnecting:
      return "connecting";
    case NtripClientState::kConnected:
      return "connected";
    case NtripClientState::kStreaming:
      return "streaming";
    case NtripClientState::kFailed:
      return "failed";
  }

  return "failed";
}

RtcmCorrectionHealthOptions BuildHealthOptions(const NtripMonitorOptions& options,
                                               const std::int64_t now_ns)
{
  RtcmCorrectionHealthOptions health_options;
  health_options.now_timestamp_ns = now_ns;
  health_options.stale_after_ns =
      std::max<std::int64_t>(5000000000LL,
                             static_cast<std::int64_t>(options.read_timeout_ms) * 5000000LL);
  health_options.required_observation_window_ns = 30000000000LL;
  health_options.startup_grace_ns = 30000000000LL;
  universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(health_options);
  return health_options;
}

void PrintSummary(const NtripMonitorOptions& options, const NtripClient& client,
                  const NtripMonitorStopReason stop_reason,
                  const std::int64_t elapsed_time_ns)
{
  const auto health = client.BuildCorrectionHealth(
      BuildHealthOptions(options, MonotonicNowNs()));
  const auto snapshot = BuildNtripMonitorSnapshot(options,
                                                  DescribeClientState(client.state()),
                                                  client.metrics(),
                                                  client.correction_monitor(),
                                                  health,
                                                  stop_reason,
                                                  elapsed_time_ns,
                                                  client.response_header());

  if (options.json_output)
  {
    std::cout << FormatNtripMonitorSummaryJson(snapshot);
  }
  else
  {
    std::cout << FormatNtripMonitorSummaryText(snapshot);
  }
}

}  // namespace

int main(int argc, char** argv)
{
  NtripMonitorOptions options;

  for (int index = 1; index < argc; ++index)
  {
    const std::string argument = argv[index];
    auto require_value = [&](const char* flag_name) -> const char* {
      if (index + 1 >= argc)
      {
        std::cerr << "error: missing value for " << flag_name << '\n';
        PrintUsage(argv[0]);
        std::exit(EXIT_FAILURE);
      }
      ++index;
      return argv[index];
    };

    if (argument == "--help" || argument == "-h")
    {
      PrintUsage(argv[0]);
      return EXIT_SUCCESS;
    }
    if (argument == "--host")
    {
      options.host = require_value("--host");
      continue;
    }
    if (argument == "--port")
    {
      if (!ParseUnsigned16(require_value("--port"), options.port))
      {
        std::cerr << "error: invalid port\n";
        return EXIT_FAILURE;
      }
      continue;
    }
    if (argument == "--mountpoint")
    {
      options.mountpoint = require_value("--mountpoint");
      continue;
    }
    if (argument == "--user")
    {
      options.username = require_value("--user");
      continue;
    }
    if (argument == "--password")
    {
      options.password = require_value("--password");
      continue;
    }
    if (argument == "--user-agent")
    {
      options.user_agent = require_value("--user-agent");
      continue;
    }
    if (argument == "--lat")
    {
      double latitude = 0.0;
      if (!ParseDouble(require_value("--lat"), latitude))
      {
        std::cerr << "error: invalid latitude\n";
        return EXIT_FAILURE;
      }
      options.latitude_deg = latitude;
      continue;
    }
    if (argument == "--lon")
    {
      double longitude = 0.0;
      if (!ParseDouble(require_value("--lon"), longitude))
      {
        std::cerr << "error: invalid longitude\n";
        return EXIT_FAILURE;
      }
      options.longitude_deg = longitude;
      continue;
    }
    if (argument == "--alt")
    {
      double altitude = 0.0;
      if (!ParseDouble(require_value("--alt"), altitude))
      {
        std::cerr << "error: invalid altitude\n";
        return EXIT_FAILURE;
      }
      options.altitude_m = altitude;
      continue;
    }
    if (argument == "--gga-interval")
    {
      std::uint32_t interval_s = 0u;
      if (!ParseUnsigned32(require_value("--gga-interval"), interval_s))
      {
        std::cerr << "error: invalid gga interval\n";
        return EXIT_FAILURE;
      }
      options.gga_interval_s = interval_s;
      continue;
    }
    if (argument == "--max-bytes")
    {
      std::size_t max_bytes = 0u;
      if (!ParseSizeT(require_value("--max-bytes"), max_bytes))
      {
        std::cerr << "error: invalid max-bytes\n";
        return EXIT_FAILURE;
      }
      options.max_bytes = max_bytes;
      continue;
    }
    if (argument == "--max-seconds")
    {
      std::uint32_t max_seconds = 0u;
      if (!ParseUnsigned32(require_value("--max-seconds"), max_seconds))
      {
        std::cerr << "error: invalid max-seconds\n";
        return EXIT_FAILURE;
      }
      options.max_seconds = max_seconds;
      continue;
    }
    if (argument == "--read-timeout-ms")
    {
      if (!ParseUnsigned32(require_value("--read-timeout-ms"), options.read_timeout_ms))
      {
        std::cerr << "error: invalid read timeout\n";
        return EXIT_FAILURE;
      }
      continue;
    }
    if (argument == "--summary")
    {
      options.summary_only = true;
      continue;
    }
    if (argument == "--json")
    {
      options.json_output = true;
      continue;
    }

    std::cerr << "error: unknown argument: " << argument << '\n';
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  const auto validation = ValidateNtripMonitorOptions(options);
  if (!validation.ok())
  {
    std::cerr << "error: " << validation.message << '\n';
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  NtripClient client(BuildNtripMonitorConfig(options));
  universal_gnss_transport::TcpClientConfig tcp_config;
  tcp_config.connect_timeout_ms = 5000u;
  tcp_config.read_timeout_ms = options.read_timeout_ms;
  tcp_config.write_timeout_ms = options.read_timeout_ms;
  client.set_tcp_config(tcp_config);

  const std::optional<universal_gnss::GnssRuntimeState> runtime_state =
      BuildNtripMonitorRuntimeState(options);

  NtripMonitorStopReason stop_reason = NtripMonitorStopReason::kCompleted;
  const std::int64_t start_time_ns = MonotonicNowNs();
  std::optional<std::int64_t> last_status_print_ns{};
  std::string last_status_line{};
  bool initial_gga_pending = runtime_state.has_value();

  auto maybe_print_status = [&](const bool force) {
    if (options.summary_only || options.json_output)
    {
      return;
    }

    const std::int64_t now_ns = MonotonicNowNs();
    if (!force && last_status_print_ns.has_value() &&
        (now_ns - *last_status_print_ns) < 1000000000LL)
    {
      return;
    }

    const auto health = client.BuildCorrectionHealth(BuildHealthOptions(options, now_ns));
    const auto snapshot = BuildNtripMonitorSnapshot(options,
                                                    DescribeClientState(client.state()),
                                                    client.metrics(),
                                                    client.correction_monitor(),
                                                    health,
                                                    NtripMonitorStopReason::kRunning,
                                                    now_ns - start_time_ns,
                                                    client.response_header());
    const std::string line = FormatNtripMonitorStatusLine(snapshot);
    if (force || line != last_status_line)
    {
      std::cout << line << '\n';
      last_status_line = line;
      last_status_print_ns = now_ns;
    }
  };

  const NtripClientError connect_error = client.Connect();
  if (connect_error != NtripClientError::kNone)
  {
    stop_reason = NtripMonitorStopReason::kConnectFailed;
    PrintSummary(options, client, stop_reason, MonotonicNowNs() - start_time_ns);
    return EXIT_FAILURE;
  }

  maybe_print_status(true);

  const NtripClientError request_error = client.SendRequest();
  if (request_error != NtripClientError::kNone)
  {
    stop_reason = NtripMonitorStopReason::kRequestFailed;
    PrintSummary(options, client, stop_reason, MonotonicNowNs() - start_time_ns);
    return EXIT_FAILURE;
  }

  if (!options.summary_only && !options.json_output)
  {
    std::cout << "request_sent mountpoint=" << options.mountpoint << '\n';
  }

  constexpr std::size_t kReadChunkBytes = 4096u;
  std::vector<std::uint8_t> buffer(kReadChunkBytes, 0u);

  while (true)
  {
    const std::int64_t now_ns = MonotonicNowNs();
    const std::int64_t elapsed_time_ns = now_ns - start_time_ns;

    if (g_interrupted != 0)
    {
      stop_reason = NtripMonitorStopReason::kInterrupted;
      break;
    }
    if (options.max_seconds.has_value() &&
        elapsed_time_ns >= static_cast<std::int64_t>(*options.max_seconds) * 1000000000LL)
    {
      stop_reason = NtripMonitorStopReason::kMaxSeconds;
      break;
    }
    if (options.max_bytes.has_value() &&
        client.metrics().bytes_received >= *options.max_bytes)
    {
      stop_reason = NtripMonitorStopReason::kMaxBytes;
      break;
    }

    const auto read_result = client.Read(buffer.data(), buffer.size(), now_ns);
    if (read_result.bytes_read > 0u)
    {
      maybe_print_status(false);
    }

    if (client.state() == NtripClientState::kStreaming && runtime_state.has_value())
    {
      if (initial_gga_pending)
      {
        const auto gga_result = client.SendGga(*runtime_state, MonotonicNowNs());
        initial_gga_pending = false;
        if (gga_result.status == NtripGgaSendStatus::kError)
        {
          stop_reason = NtripMonitorStopReason::kGgaSendFailed;
          break;
        }
        maybe_print_status(true);
      }
      else if (options.gga_interval_s.has_value())
      {
        const auto gga_result = client.MaybeInjectGga(*runtime_state, MonotonicNowNs());
        if (gga_result.status == NtripGgaSendStatus::kError)
        {
          stop_reason = NtripMonitorStopReason::kGgaSendFailed;
          break;
        }
      }
    }

    if (read_result.bytes_read == 0u)
    {
      if (read_result.client_error == NtripClientError::kNone)
      {
        maybe_print_status(false);
        continue;
      }

      stop_reason = read_result.client_error == NtripClientError::kDisconnected
                        ? NtripMonitorStopReason::kDisconnected
                        : NtripMonitorStopReason::kReadError;
      break;
    }

    if (options.max_bytes.has_value() &&
        client.metrics().bytes_received >= *options.max_bytes)
    {
      stop_reason = NtripMonitorStopReason::kMaxBytes;
      break;
    }
  }

  const std::int64_t final_elapsed_ns = MonotonicNowNs() - start_time_ns;
  PrintSummary(options, client, stop_reason, final_elapsed_ns);

  switch (stop_reason)
  {
    case NtripMonitorStopReason::kConnectFailed:
    case NtripMonitorStopReason::kRequestFailed:
    case NtripMonitorStopReason::kGgaSendFailed:
    case NtripMonitorStopReason::kReadError:
    case NtripMonitorStopReason::kDisconnected:
      return EXIT_FAILURE;
    case NtripMonitorStopReason::kRunning:
    case NtripMonitorStopReason::kCompleted:
    case NtripMonitorStopReason::kMaxBytes:
    case NtripMonitorStopReason::kMaxSeconds:
    case NtripMonitorStopReason::kInterrupted:
      return EXIT_SUCCESS;
  }

  return EXIT_SUCCESS;
}
