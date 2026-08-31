#include <cstdlib>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "universal_gnss_tools/gnss_replay.hpp"

namespace
{

void PrintUsage(const char* program_name)
{
  std::cout
      << "Usage: " << program_name << " [--summary] [--json] [--timing-mode fast|wall_time] [--speed N] [--fallback-step-ms N] [path|-]\n"
      << "Examples:\n"
      << "  " << program_name << " file.bin\n"
      << "  cat file.bin | " << program_name << " -\n"
      << "  " << program_name << " --summary file.bin\n"
      << "  " << program_name << " --json file.bin\n";
}

}  // namespace

int main(int argc, char** argv)
{
  bool summary_only = false;
  bool json_output = false;
  universal_gnss_tools::GnssReplayTimingConfig timing_config;
  std::string input_path = "-";
  bool input_path_set = false;

  for (int index = 1; index < argc; ++index)
  {
    const std::string argument = argv[index];
    if (argument == "--summary")
    {
      summary_only = true;
      continue;
    }

    if (argument == "--json")
    {
      json_output = true;
      continue;
    }
    if (argument == "--timing-mode" && index + 1 < argc)
    {
      const std::string mode = argv[++index];
      if (mode == "fast")
      {
        timing_config.mode = universal_gnss_tools::GnssReplayTimingMode::kFast;
      }
      else if (mode == "wall_time")
      {
        timing_config.mode = universal_gnss_tools::GnssReplayTimingMode::kWallTime;
      }
      else
      {
        std::cerr << "error: --timing-mode must be fast or wall_time\n";
        return EXIT_FAILURE;
      }
      continue;
    }
    if ((argument == "--speed" || argument == "--fallback-step-ms") && index + 1 < argc)
    {
      try {
        if (argument == "--speed") timing_config.speed = std::stod(argv[++index]);
        else timing_config.fallback_step = std::chrono::milliseconds(std::stoll(argv[++index]));
      }
      catch (const std::exception&)
      {
        std::cerr << "error: invalid " << argument << " value\n";
        return EXIT_FAILURE;
      }
      continue;
    }

    if (argument == "--help" || argument == "-h")
    {
      PrintUsage(argv[0]);
      return EXIT_SUCCESS;
    }

    if (input_path_set)
    {
      std::cerr << "error: only one input path is supported\n";
      PrintUsage(argv[0]);
      return EXIT_FAILURE;
    }

    input_path = argument;
    input_path_set = true;
  }

  std::ifstream file_stream;
  std::istream* input = &std::cin;
  if (input_path != "-")
  {
    file_stream.open(input_path, std::ios::binary);
    if (!file_stream.is_open())
    {
      std::cerr << "error: could not open input file: " << input_path << '\n';
      return EXIT_FAILURE;
    }
    input = &file_stream;
  }

  const auto result = universal_gnss_tools::ReplayGnssStream(*input, !summary_only);
  if (input->bad())
  {
    std::cerr << "error: failed while reading input data\n";
    return EXIT_FAILURE;
  }

  if (!std::isfinite(timing_config.speed) || !(timing_config.speed > 0.0) ||
      timing_config.fallback_step.count() <= 0)
  {
    std::cerr << "error: timing values must be finite and strictly positive\n";
    return EXIT_FAILURE;
  }
  if (timing_config.mode == universal_gnss_tools::GnssReplayTimingMode::kWallTime &&
      (json_output || summary_only))
  {
    std::cerr << "error: wall_time timing requires text event output\n";
    return EXIT_FAILURE;
  }

  if (timing_config.mode == universal_gnss_tools::GnssReplayTimingMode::kWallTime)
  {
    const auto plan = universal_gnss_tools::BuildGnssReplayTimingPlan(result, timing_config);
    for (std::size_t index = 0u; index < result.events.size(); ++index)
    {
      std::this_thread::sleep_for(plan[index].delay_before_event);
      std::cout << universal_gnss_tools::FormatGnssReplayEventText(result.events[index]);
    }
    std::cout << universal_gnss_tools::FormatGnssReplayText(result, true);
  }
  else if (json_output)
  {
    std::cout << universal_gnss_tools::FormatGnssReplayJson(result, summary_only);
  }
  else
  {
    std::cout << universal_gnss_tools::FormatGnssReplayText(result, summary_only);
  }

  return EXIT_SUCCESS;
}
