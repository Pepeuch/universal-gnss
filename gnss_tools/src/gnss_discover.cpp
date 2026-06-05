#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_discovery.hpp"
#include "universal_gnss_tools/receiver_discovery_format.hpp"

namespace
{

struct DiscoverOptions
{
  std::optional<std::string> explicit_path{};
  bool json_output{false};
  bool allow_generic_nmea{true};
  bool include_platform_uarts{false};
  std::vector<std::uint32_t> baud_candidates{};
};

void PrintUsage(const char* program_name)
{
  std::cout
      << "Usage: " << program_name
      << " [--path <device>] [--json] [--baud 921600,115200] [--allow-nmea]"
      << " [--include-platform-uarts]\n"
      << "Examples:\n"
      << "  " << program_name << '\n'
      << "  " << program_name
      << " --path /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00\n"
      << "  " << program_name << " --json\n"
      << "  " << program_name << " --baud 921600,115200 --allow-nmea\n"
      << "  " << program_name << " --include-platform-uarts\n"
      << "  " << program_name << " --path /dev/ttyAMA2 --baud 921600\n"
      << "Notes:\n"
      << "  stable /dev/serial/by-id/* paths are preferred when available\n";
}

bool ParseUnsigned32(const std::string& text, std::uint32_t& value)
{
  try
  {
    std::size_t consumed = 0u;
    const auto parsed = std::stoul(text, &consumed, 10);
    if (consumed != text.size())
    {
      return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

bool ParseBaudList(const std::string& text, std::vector<std::uint32_t>& values)
{
  values.clear();
  std::size_t start = 0u;
  while (start <= text.size())
  {
    const std::size_t comma = text.find(',', start);
    const std::string item =
        text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (item.empty())
    {
      return false;
    }

    std::uint32_t baud = 0u;
    if (!ParseUnsigned32(item, baud))
    {
      return false;
    }
    values.push_back(baud);

    if (comma == std::string::npos)
    {
      break;
    }
    start = comma + 1u;
  }

  return !values.empty();
}

}  // namespace

int main(int argc, char** argv)
{
  DiscoverOptions options;

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
    if (argument == "--path")
    {
      options.explicit_path = require_value("--path");
      continue;
    }
    if (argument == "--json")
    {
      options.json_output = true;
      continue;
    }
    if (argument == "--allow-nmea")
    {
      options.allow_generic_nmea = true;
      continue;
    }
    if (argument == "--include-platform-uarts")
    {
      options.include_platform_uarts = true;
      continue;
    }
    if (argument == "--baud")
    {
      if (!ParseBaudList(require_value("--baud"), options.baud_candidates))
      {
        std::cerr << "error: invalid --baud list\n";
        return EXIT_FAILURE;
      }
      continue;
    }

    std::cerr << "error: unknown argument '" << argument << "'\n";
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  universal_gnss_driver::ReceiverProbeConfig config;
  if (!options.baud_candidates.empty())
  {
    config.baud_candidates = options.baud_candidates;
  }
  config.allow_generic_nmea_fallback = options.allow_generic_nmea;
  config.include_platform_uarts = options.include_platform_uarts;

  const auto results = universal_gnss_driver::DiscoverReceivers(config, options.explicit_path);
  if (options.json_output)
  {
    std::cout << universal_gnss_tools::FormatReceiverDiscoveryJson(results);
  }
  else
  {
    std::cout << universal_gnss_tools::FormatReceiverDiscoveryText(results);
  }

  return EXIT_SUCCESS;
}
