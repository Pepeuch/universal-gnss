#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "universal_gnss_tools/gnss_log_comparison.hpp"

namespace
{

void PrintUsage(const char* program_name)
{
  std::cout << "Usage: " << program_name << " [--json] left-log right-log\n"
            << "Examples:\n"
            << "  " << program_name << " receiver-a.bin receiver-b.bin\n"
            << "  " << program_name << " --json receiver-a.bin receiver-b.bin\n";
}

}  // namespace

int main(int argc, char** argv)
{
  bool json_output = false;
  std::string left_path;
  std::string right_path;

  for (int index = 1; index < argc; ++index)
  {
    const std::string argument = argv[index];
    if (argument == "--json")
    {
      json_output = true;
      continue;
    }
    if (argument == "--help" || argument == "-h")
    {
      PrintUsage(argv[0]);
      return EXIT_SUCCESS;
    }
    if (left_path.empty())
    {
      left_path = argument;
      continue;
    }
    if (right_path.empty())
    {
      right_path = argument;
      continue;
    }

    std::cerr << "error: exactly two input paths are required\n";
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  if (left_path.empty() || right_path.empty() || left_path == "-" || right_path == "-")
  {
    std::cerr << "error: exactly two file paths are required; stdin is not supported\n";
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  std::ifstream left_input(left_path, std::ios::binary);
  std::ifstream right_input(right_path, std::ios::binary);
  if (!left_input.is_open() || !right_input.is_open())
  {
    std::cerr << "error: could not open both input files\n";
    return EXIT_FAILURE;
  }

  const auto left_report = universal_gnss_tools::BuildGnssQualityReportStream(left_input);
  const auto right_report = universal_gnss_tools::BuildGnssQualityReportStream(right_input);
  if (left_input.bad() || right_input.bad())
  {
    std::cerr << "error: failed while reading input data\n";
    return EXIT_FAILURE;
  }

  const auto comparison =
      universal_gnss_tools::CompareGnssQualityReports(left_report, right_report);
  if (json_output)
  {
    std::cout << universal_gnss_tools::FormatGnssLogComparisonJson(comparison);
  }
  else
  {
    std::cout << universal_gnss_tools::FormatGnssLogComparisonText(comparison);
  }
  return EXIT_SUCCESS;
}
