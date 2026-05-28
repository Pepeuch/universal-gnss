#pragma once

#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace universal_gnss_tools::test
{

inline std::string TestdataPath(const std::string& relative_path)
{
#ifndef TESTDATA_DIR
#error "TESTDATA_DIR must be defined for gnss_tools tests"
#endif

  return std::string(TESTDATA_DIR) + "/" + relative_path;
}

inline std::vector<std::uint8_t> ReadBinaryFile(const std::string& relative_path)
{
  const std::string path = TestdataPath(relative_path);
  std::ifstream input(path, std::ios::binary);
  if (!input)
  {
    throw std::runtime_error("failed to open test data file: " + path);
  }

  const std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
  return std::vector<std::uint8_t>(content.begin(), content.end());
}

}  // namespace universal_gnss_tools::test
