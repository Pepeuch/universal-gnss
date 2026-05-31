#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "universal_gnss_tools/gnss_replay.hpp"
#include "universal_gnss_tools/runtime_export.hpp"

namespace
{

void PrintUsage(const char* program_name)
{
  std::cout
      << "Usage: " << program_name
      << " [--format jsonl] [--output path] [--pretty] [path|-]\n"
      << "Examples:\n"
      << "  " << program_name << " log.bin\n"
      << "  " << program_name << " --format jsonl log.bin\n"
      << "  " << program_name << " --output runtime.jsonl log.bin\n"
      << "  " << program_name << " --pretty log.bin\n";
}

}  // namespace

int main(int argc, char** argv)
{
  universal_gnss_tools::RuntimeExportOptions options;
  std::string input_path = "-";
  std::string output_path{};
  bool input_path_set = false;

  for (int index = 1; index < argc; ++index)
  {
    const std::string argument = argv[index];
    if (argument == "--pretty")
    {
      options.pretty = true;
      continue;
    }
    if (argument == "--help" || argument == "-h")
    {
      PrintUsage(argv[0]);
      return EXIT_SUCCESS;
    }
    if (argument == "--format")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "error: --format requires a value\n";
        return EXIT_FAILURE;
      }

      const std::string format = argv[++index];
      if (format != "jsonl")
      {
        std::cerr << "error: unsupported export format: " << format
                  << " (only jsonl is implemented)\n";
        return EXIT_FAILURE;
      }
      options.format = universal_gnss_tools::RuntimeExportFormat::kJsonl;
      continue;
    }
    if (argument == "--output")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "error: --output requires a path\n";
        return EXIT_FAILURE;
      }

      output_path = argv[++index];
      continue;
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

  const auto replay_result = universal_gnss_tools::ReplayGnssStream(*input, true);
  if (input->bad())
  {
    std::cerr << "error: failed while reading input data\n";
    return EXIT_FAILURE;
  }

  std::ofstream output_file;
  std::ostream* output = &std::cout;
  if (!output_path.empty())
  {
    output_file.open(output_path, std::ios::binary);
    if (!output_file.is_open())
    {
      std::cerr << "error: could not open output file: " << output_path << '\n';
      return EXIT_FAILURE;
    }
    output = &output_file;
  }

  switch (options.format)
  {
    case universal_gnss_tools::RuntimeExportFormat::kJsonl:
      universal_gnss_tools::WriteRuntimeExportJsonl(*output, replay_result, options);
      break;
  }

  if (!output->good())
  {
    std::cerr << "error: failed while writing export output\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
