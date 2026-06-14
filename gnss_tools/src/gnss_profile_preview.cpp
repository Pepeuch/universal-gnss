#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "universal_gnss_tools/profile_preview.hpp"

namespace
{

void PrintUsage(const char* program_name)
{
  std::cout
      << "Usage: " << program_name
      << " [--json] [--verbose] [--persistent]"
      << " [--signal-profile <balanced|high_precision|all_signals|minimal|custom>]"
      << " [--output-port <usb|uart1|uart2|all|auto>]"
      << " [--config-baud <value>] [--rate-hz <value>] <vendor> <profile>\n"
      << "Examples:\n"
      << "  " << program_name << " ublox rover_high_precision\n"
      << "  " << program_name << " ublox rover_high_precision_debug --json\n"
      << "  " << program_name << " ublox rover_high_precision --output-port all\n"
      << "  " << program_name << " unicore factory_reset\n"
      << "  " << program_name << " unicore rover_high_precision --persistent\n"
      << "  " << program_name << " unicore rover_high_precision --signal-profile minimal\n"
      << "  " << program_name << " nmea runtime_only\n"
      << "Notes:\n"
      << "  --baud remains accepted as a legacy alias for --config-baud\n";
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
  bool verbose = false;
  universal_gnss_tools::ProfilePreviewOptions options;
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

    if (argument == "--verbose")
    {
      verbose = true;
      continue;
    }

    if (argument == "--persistent")
    {
      options.persistent = true;
      continue;
    }

    if (argument == "--baud" || argument == "--config-baud")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "error: " << argument << " requires a value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      std::uint32_t baud = 0u;
      if (!ParseUnsigned(argv[++index], baud))
      {
        std::cerr << "error: invalid " << argument << " value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      options.baud = baud;
      continue;
    }

    if (argument == "--signal-profile")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "error: --signal-profile requires a value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      const auto parsed =
          universal_gnss_driver::ParseReceiverAutoConfigSignalProfile(argv[++index]);
      if (!parsed.has_value())
      {
        std::cerr << "error: invalid --signal-profile value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      options.signal_profile = *parsed;
      continue;
    }

    if (argument == "--output-port")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "error: --output-port requires a value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      const auto parsed =
          universal_gnss_driver::ParseReceiverAutoConfigOutputPort(argv[++index]);
      if (!parsed.has_value())
      {
        std::cerr << "error: invalid --output-port value\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
      }

      options.output_port = *parsed;
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

  const auto result = universal_gnss_tools::BuildProfilePreview(options);
  if (json_output)
  {
    std::cout << universal_gnss_tools::FormatProfilePreviewJson(result, verbose);
  }
  else
  {
    std::cout << universal_gnss_tools::FormatProfilePreviewText(result, verbose);
  }

  return result.status == universal_gnss_tools::ProfilePreviewStatus::kOk ? EXIT_SUCCESS
                                                                           : EXIT_FAILURE;
}
