#include <cstdlib>
#include <iostream>
#include <string>

#include "universal_gnss_tools/config_apply.hpp"

#if defined(__linux__)
#include "universal_gnss_transport/posix_serial_transport.hpp"
#endif

namespace
{

void PrintUsage(const char* program_name)
{
  std::cout
      << "Usage: " << program_name
      << " [--json] [--persistent] [--rate-hz <value>] [--timeout-ms <value>]"
      << " [--port <path> --baud <value>] [--execute] [--confirm-runtime]"
      << " [--confirm-persistent] <vendor> <profile>\n"
      << "Examples:\n"
      << "  " << program_name << " ublox rover --port /dev/ttyACM0 --baud 921600\n"
      << "  " << program_name
      << " ublox rover --port /dev/ttyACM0 --baud 921600 --execute --confirm-runtime\n"
      << "  " << program_name
      << " unicore diagnostics --port /dev/ttyUSB0 --baud 921600 --execute --confirm-runtime\n"
      << "  " << program_name
      << " ublox rover --persistent --port /dev/ttyACM0 --baud 921600 --execute --confirm-persistent\n";
}

bool ParseUnsigned(const std::string& text, std::uint32_t& value)
{
  std::size_t parsed = 0u;
  try
  {
    const auto numeric = std::stoul(text, &parsed, 10);
    if (parsed != text.size())
    {
      return false;
    }
    value = static_cast<std::uint32_t>(numeric);
    return true;
  }
  catch (...)
  {
    return false;
  }
}

bool ParseDouble(const std::string& text, double& value)
{
  std::size_t parsed = 0u;
  try
  {
    value = std::stod(text, &parsed);
    return parsed == text.size();
  }
  catch (...)
  {
    return false;
  }
}

}  // namespace

int main(int argc, char** argv)
{
  bool json_output = false;
  universal_gnss_tools::ConfigApplyOptions options;
  bool vendor_set = false;
  bool profile_set = false;

  for (int index = 1; index < argc; ++index)
  {
    const std::string argument = argv[index];
    if (argument == "--json")
    {
      json_output = true;
      continue;
    }

    if (argument == "--persistent")
    {
      options.persistent = true;
      continue;
    }

    if (argument == "--execute")
    {
      options.execute = true;
      continue;
    }

    if (argument == "--confirm-runtime")
    {
      options.confirm_runtime = true;
      continue;
    }

    if (argument == "--confirm-persistent")
    {
      options.confirm_persistent = true;
      continue;
    }

    if (argument == "--port")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "error: --port requires a value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      options.port = argv[++index];
      continue;
    }

    if (argument == "--baud")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "error: --baud requires a value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      if (!ParseUnsigned(argv[++index], options.transport_baud_rate))
      {
        std::cerr << "error: invalid --baud value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      continue;
    }

    if (argument == "--timeout-ms")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "error: --timeout-ms requires a value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      if (!ParseUnsigned(argv[++index], options.timeout_ms))
      {
        std::cerr << "error: invalid --timeout-ms value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      continue;
    }

    if (argument == "--rate-hz")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "error: --rate-hz requires a value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      double rate_hz = 0.0;
      if (!ParseDouble(argv[++index], rate_hz))
      {
        std::cerr << "error: invalid --rate-hz value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      options.rate_hz = rate_hz;
      continue;
    }

    if (argument == "--help" || argument == "-h")
    {
      PrintUsage(argv[0]);
      return EXIT_SUCCESS;
    }

    if (!vendor_set)
    {
      options.vendor = argument;
      vendor_set = true;
      continue;
    }

    if (!profile_set)
    {
      options.profile = argument;
      profile_set = true;
      continue;
    }

    std::cerr << "error: unexpected extra argument: " << argument << '\n';
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  if (!vendor_set || !profile_set)
  {
    std::cerr << "error: vendor and profile are required\n";
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  if (options.execute)
  {
    if (options.port.empty())
    {
      std::cerr << "error: --port is required with --execute\n";
      return EXIT_FAILURE;
    }

    if (options.transport_baud_rate == 0u)
    {
      std::cerr << "error: --baud is required with --execute\n";
      return EXIT_FAILURE;
    }
  }

  const auto prepared = universal_gnss_tools::PrepareConfigApply(options);
  if (!options.execute || prepared.status != universal_gnss_tools::ConfigApplyStatus::kOk)
  {
    if (json_output)
    {
      std::cout << universal_gnss_tools::FormatConfigApplyJson(prepared);
    }
    else
    {
      std::cout << universal_gnss_tools::FormatConfigApplyText(prepared);
    }

    return prepared.status == universal_gnss_tools::ConfigApplyStatus::kOk ? EXIT_SUCCESS
                                                                            : EXIT_FAILURE;
  }

#if defined(__linux__)
  universal_gnss_transport::PosixSerialTransport transport;
  universal_gnss_transport::PosixSerialConfig serial_config;
  serial_config.device_path = options.port;
  serial_config.baud_rate = options.transport_baud_rate;
  serial_config.read_timeout_ms =
      options.timeout_ms > 100u ? 100u : (options.timeout_ms == 0u ? 1u : options.timeout_ms);

  const auto open_error = transport.Open(serial_config);
  if (open_error != universal_gnss_transport::TransportError::kNone)
  {
    auto failed = prepared;
    failed.status = universal_gnss_tools::ConfigApplyStatus::kTransportUnavailable;
    failed.error_message = "failed to open configured serial transport";
    failed.execution_summary.final_status = "transport_unavailable";

    if (json_output)
    {
      std::cout << universal_gnss_tools::FormatConfigApplyJson(failed);
    }
    else
    {
      std::cout << universal_gnss_tools::FormatConfigApplyText(failed);
    }
    return EXIT_FAILURE;
  }

  const auto result = universal_gnss_tools::ExecuteConfigApply(transport, options);
  if (json_output)
  {
    std::cout << universal_gnss_tools::FormatConfigApplyJson(result);
  }
  else
  {
    std::cout << universal_gnss_tools::FormatConfigApplyText(result);
  }

  return result.status == universal_gnss_tools::ConfigApplyStatus::kOk ? EXIT_SUCCESS
                                                                        : EXIT_FAILURE;
#else
  auto unsupported = prepared;
  unsupported.status = universal_gnss_tools::ConfigApplyStatus::kTransportUnavailable;
  unsupported.error_message = "gnss_config_apply requires Linux POSIX serial support";
  unsupported.execution_summary.final_status = "transport_unavailable";

  if (json_output)
  {
    std::cout << universal_gnss_tools::FormatConfigApplyJson(unsupported);
  }
  else
  {
    std::cout << universal_gnss_tools::FormatConfigApplyText(unsupported);
  }
  return EXIT_FAILURE;
#endif
}
