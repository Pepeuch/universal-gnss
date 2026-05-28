#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "universal_gnss_tools/gnss_replay.hpp"

namespace
{

void PrintUsage(const char* program_name)
{
  std::cout
      << "Usage: " << program_name << " [--summary] [--json] [path|-]\n"
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

  if (json_output)
  {
    std::cout << universal_gnss_tools::FormatGnssReplayJson(result, summary_only);
  }
  else
  {
    std::cout << universal_gnss_tools::FormatGnssReplayText(result, summary_only);
  }

  return EXIT_SUCCESS;
}
