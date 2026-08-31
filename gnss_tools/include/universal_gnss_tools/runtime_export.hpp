#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

#include "universal_gnss_tools/gnss_replay.hpp"

namespace universal_gnss_tools
{

enum class RuntimeExportFormat : std::uint8_t
{
  kJsonl = 0,
  kCsv = 1,
};

struct RuntimeExportOptions
{
  RuntimeExportFormat format{RuntimeExportFormat::kJsonl};
  bool pretty{false};
};

const char* DescribeRuntimeExportFormat(RuntimeExportFormat format);

std::string FormatRuntimeExportJsonl(const GnssReplayResult& replay_result,
                                     const RuntimeExportOptions& options = {});

std::size_t WriteRuntimeExportJsonl(std::ostream& output,
                                    const GnssReplayResult& replay_result,
                                    const RuntimeExportOptions& options = {});

std::string FormatRuntimeExportCsv(const GnssReplayResult& replay_result);

std::size_t WriteRuntimeExportCsv(std::ostream& output, const GnssReplayResult& replay_result);

}  // namespace universal_gnss_tools
