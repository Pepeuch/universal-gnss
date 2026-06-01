#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_driver/receiver_session_runner.hpp"
#include "universal_gnss_tools/runtime_state_format.hpp"
#include "universal_gnss_transport/posix_serial_transport.hpp"

namespace
{

using universal_gnss_driver::ReceiverSession;
using universal_gnss_driver::ReceiverSessionConfig;
using universal_gnss_driver::ReceiverSessionKind;
using universal_gnss_driver::ReceiverSessionRunner;
using universal_gnss_driver::ReceiverSessionRunnerConfig;
using universal_gnss_transport::PosixSerialConfig;
using universal_gnss_transport::PosixSerialTransport;
using universal_gnss_transport::TransportError;
using universal_gnss_transport::TransportStatus;

struct MonitorOptions
{
  std::string port{};
  std::uint32_t baud_rate{0u};
  ReceiverSessionKind vendor{ReceiverSessionKind::kAutoDetect};
  std::size_t chunk_size{512u};
  std::optional<std::size_t> max_bytes{};
  bool summary_only{false};
  bool json_output{false};
};

void PrintUsage(const char* program_name)
{
  std::cout
      << "Usage: " << program_name
      << " --port <path> --baud <int> [--vendor auto|ublox|unicore|nmea]"
      << " [--chunk-size <bytes>] [--max-bytes <bytes>] [--summary] [--json]\n"
      << "Examples:\n"
      << "  " << program_name << " --port /dev/ttyACM0 --baud 921600 --vendor auto\n"
      << "  " << program_name << " --port /dev/ttyUSB0 --baud 115200 --vendor ublox\n"
      << "  " << program_name << " --port /dev/ttyUSB0 --baud 921600 --vendor unicore\n"
      << "  " << program_name << " --port /dev/ttyUSB0 --baud 115200 --vendor nmea\n";
}

bool ParseUnsigned(const std::string& text, std::size_t& value)
{
  try
  {
    std::size_t consumed = 0u;
    const auto parsed = std::stoull(text, &consumed, 10);
    if (consumed != text.size())
    {
      return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

bool ParseUnsigned32(const std::string& text, std::uint32_t& value)
{
  std::size_t parsed = 0u;
  if (!ParseUnsigned(text, parsed))
  {
    return false;
  }
  value = static_cast<std::uint32_t>(parsed);
  return true;
}

std::optional<ReceiverSessionKind> ParseVendor(const std::string& text)
{
  if (text == "auto")
  {
    return ReceiverSessionKind::kAutoDetect;
  }
  if (text == "ublox")
  {
    return ReceiverSessionKind::kUblox;
  }
  if (text == "unicore")
  {
    return ReceiverSessionKind::kUnicore;
  }
  if (text == "nmea")
  {
    return ReceiverSessionKind::kNmea;
  }
  return std::nullopt;
}

const char* ToString(const TransportStatus status)
{
  switch (status)
  {
    case TransportStatus::kOk:
      return "ok";
    case TransportStatus::kEndOfStream:
      return "eof";
    case TransportStatus::kClosed:
      return "closed";
    case TransportStatus::kError:
      return "error";
  }

  return "error";
}

const char* ToString(const TransportError error)
{
  switch (error)
  {
    case TransportError::kNone:
      return "none";
    case TransportError::kClosed:
      return "closed";
    case TransportError::kInvalidArgument:
      return "invalid_argument";
    case TransportError::kOverflow:
      return "overflow";
    case TransportError::kReadFailure:
      return "read_failure";
    case TransportError::kWriteFailure:
      return "write_failure";
    case TransportError::kUnsupported:
      return "unsupported";
    case TransportError::kUnknown:
      return "unknown";
  }

  return "unknown";
}

std::string FormatSummaryText(const MonitorOptions& options,
                              const ReceiverSession& session,
                              const ReceiverSessionRunner& runner)
{
  std::ostringstream stream;
  stream
      << "Summary:\n"
      << "  port=" << options.port
      << " baud=" << options.baud_rate
      << " vendor=" << universal_gnss_driver::ToString(options.vendor) << '\n'
      << "  selected_session="
      << (session.metrics().selected_session_kind.has_value()
              ? universal_gnss_driver::ToString(*session.metrics().selected_session_kind)
              : "undecided")
      << " bytes_read=" << runner.metrics().bytes_read
      << " chunks_read=" << runner.metrics().chunks_read
      << " runtime_updates=" << session.metrics().runtime_updates << '\n'
      << "  eof_seen=" << std::boolalpha << runner.metrics().eof_seen
      << " read_errors=" << runner.metrics().read_errors
      << " malformed_records=" << session.metrics().malformed_records
      << " unknown_records=" << session.metrics().unknown_records
      << " last_status=" << ToString(runner.metrics().last_status)
      << " last_error=" << ToString(runner.metrics().last_error) << '\n'
      << "  final_state: "
      << universal_gnss_tools::FormatRuntimeStateCompact(
             session.current_state(), session.metrics().selected_session_kind)
      << '\n';
  return stream.str();
}

std::string FormatSummaryJson(const MonitorOptions& options,
                              const ReceiverSession& session,
                              const ReceiverSessionRunner& runner)
{
  std::ostringstream stream;
  stream
      << "{"
      << "\"type\":\"summary\","
      << "\"port\":\"" << options.port << "\","
      << "\"baud\":" << options.baud_rate << ','
      << "\"configured_vendor\":\"" << universal_gnss_driver::ToString(options.vendor) << "\","
      << "\"selected_session\":";
  if (session.metrics().selected_session_kind.has_value())
  {
    stream << '"' << universal_gnss_driver::ToString(*session.metrics().selected_session_kind)
           << '"';
  }
  else
  {
    stream << "null";
  }
  stream
      << ','
      << "\"bytes_read\":" << runner.metrics().bytes_read << ','
      << "\"chunks_read\":" << runner.metrics().chunks_read << ','
      << "\"eof_seen\":" << (runner.metrics().eof_seen ? "true" : "false") << ','
      << "\"read_errors\":" << runner.metrics().read_errors << ','
      << "\"runtime_updates\":" << session.metrics().runtime_updates << ','
      << "\"malformed_records\":" << session.metrics().malformed_records << ','
      << "\"unknown_records\":" << session.metrics().unknown_records << ','
      << "\"last_status\":\"" << ToString(runner.metrics().last_status) << "\","
      << "\"last_error\":\"" << ToString(runner.metrics().last_error) << "\","
      << "\"final_state\":"
      << universal_gnss_tools::FormatRuntimeStateJson(
             session.current_state(), session.metrics().selected_session_kind)
      << "}\n";
  return stream.str();
}

}  // namespace

int main(int argc, char** argv)
{
  MonitorOptions options;

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
    if (argument == "--port")
    {
      options.port = require_value("--port");
      continue;
    }
    if (argument == "--baud")
    {
      if (!ParseUnsigned32(require_value("--baud"), options.baud_rate))
      {
        std::cerr << "error: invalid baud rate\n";
        return EXIT_FAILURE;
      }
      continue;
    }
    if (argument == "--vendor")
    {
      const auto vendor = ParseVendor(require_value("--vendor"));
      if (!vendor.has_value())
      {
        std::cerr << "error: vendor must be auto, ublox, unicore, or nmea\n";
        return EXIT_FAILURE;
      }
      options.vendor = *vendor;
      continue;
    }
    if (argument == "--chunk-size")
    {
      if (!ParseUnsigned(require_value("--chunk-size"), options.chunk_size))
      {
        std::cerr << "error: invalid chunk size\n";
        return EXIT_FAILURE;
      }
      continue;
    }
    if (argument == "--max-bytes")
    {
      std::size_t max_bytes = 0u;
      if (!ParseUnsigned(require_value("--max-bytes"), max_bytes))
      {
        std::cerr << "error: invalid max-bytes\n";
        return EXIT_FAILURE;
      }
      options.max_bytes = max_bytes;
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

  if (options.port.empty())
  {
    std::cerr << "error: --port is required\n";
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }
  if (options.baud_rate == 0u)
  {
    std::cerr << "error: --baud is required\n";
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  PosixSerialTransport serial;
  const auto open_error = serial.Open(PosixSerialConfig{options.port, options.baud_rate, false, 0u});
  if (open_error != TransportError::kNone)
  {
    std::cerr << "error: failed to open serial port " << options.port
              << ": " << ToString(open_error) << '\n';
    return EXIT_FAILURE;
  }

  ReceiverSession session(ReceiverSessionConfig{options.vendor});
  ReceiverSessionRunner runner(serial, session, ReceiverSessionRunnerConfig{options.chunk_size});

  std::string last_line{};
  std::size_t last_runtime_updates = session.metrics().runtime_updates;
  auto last_selected_kind = session.metrics().selected_session_kind;

  while (true)
  {
    if (options.max_bytes.has_value() && runner.metrics().bytes_read >= *options.max_bytes)
    {
      break;
    }

    const bool keep_running = runner.StepOnce();

    const bool selected_changed = session.metrics().selected_session_kind != last_selected_kind;
    const bool updates_changed = session.metrics().runtime_updates != last_runtime_updates;
    if (!options.summary_only && (selected_changed || updates_changed))
    {
      if (options.json_output)
      {
        std::cout
            << "{\"type\":\"update\",\"runtime_updates\":"
            << session.metrics().runtime_updates
            << ",\"bytes_read\":" << runner.metrics().bytes_read
            << ",\"state\":"
            << universal_gnss_tools::FormatRuntimeStateJson(
                   session.current_state(), session.metrics().selected_session_kind)
            << "}\n";
      }
      else
      {
        const std::string line = universal_gnss_tools::FormatRuntimeStateCompact(
            session.current_state(), session.metrics().selected_session_kind);
        if (line != last_line || selected_changed)
        {
          std::cout << line << '\n';
          last_line = line;
        }
      }
    }

    last_runtime_updates = session.metrics().runtime_updates;
    last_selected_kind = session.metrics().selected_session_kind;

    if (!keep_running)
    {
      break;
    }
  }

  if (options.json_output)
  {
    std::cout << FormatSummaryJson(options, session, runner);
  }
  else
  {
    std::cout << FormatSummaryText(options, session, runner);
  }

  return runner.metrics().last_status == TransportStatus::kError ? EXIT_FAILURE : EXIT_SUCCESS;
}
