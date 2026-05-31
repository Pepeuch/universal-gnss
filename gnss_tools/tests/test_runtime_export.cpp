#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "universal_gnss_tools/gnss_replay.hpp"
#include "universal_gnss_tools/runtime_export.hpp"
#include "testdata_utils.hpp"

namespace
{

struct TestContext
{
  int failures{0};

  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

std::vector<std::string> SplitLines(const std::string& text)
{
  std::vector<std::string> lines;
  std::istringstream input(text);
  std::string line;
  while (std::getline(input, line))
  {
    if (!line.empty())
    {
      lines.push_back(line);
    }
  }
  return lines;
}

void TestNmeaJsonlExport(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("nmea/basic_fix.nmea");
  const auto replay = universal_gnss_tools::ReplayGnssBytes(bytes, true);
  const std::string jsonl = universal_gnss_tools::FormatRuntimeExportJsonl(replay);
  const auto lines = SplitLines(jsonl);

  ctx.Expect(lines.size() == 5u,
             "basic NMEA export should emit one JSON line per runtime update");
  ctx.Expect(lines.front().front() == '{' && lines.front().back() == '}',
             "each JSONL export line should be a single JSON object");
  ctx.Expect(lines.front().find("\"protocol\":\"NMEA\"") != std::string::npos &&
                 lines.front().find("\"message\":\"GGA\"") != std::string::npos,
             "the first NMEA export line should identify the GGA update");
  ctx.Expect(lines.back().find("\"message\":\"GST\"") != std::string::npos &&
                 lines.back().find("\"horizontal_accuracy_m\":0.6") != std::string::npos &&
                 lines.back().find("\"vertical_accuracy_m\":1.1") != std::string::npos,
             "the GST export line should carry conservative accuracy values");
  ctx.Expect(lines.front().find("\"satellites_tracked\":null") != std::string::npos &&
                 lines.front().find("\"interference_detected\":null") != std::string::npos,
             "missing optional fields should be exported as null consistently");
}

void TestMixedJsonlExport(TestContext& ctx)
{
  const auto bytes =
      universal_gnss_tools::test::ReadBinaryFile("mixed/nmea_ubx_rtcm_unicore.bin");
  const auto replay = universal_gnss_tools::ReplayGnssBytes(bytes, true);
  const std::string jsonl = universal_gnss_tools::FormatRuntimeExportJsonl(replay);
  const auto lines = SplitLines(jsonl);

  ctx.Expect(lines.size() == replay.summary.runtime_updates && lines.size() == 7u,
             "mixed export should emit only runtime updates, not every parsed frame");
  ctx.Expect(jsonl.find("\"protocol\":\"UBX\"") != std::string::npos &&
                 jsonl.find("\"message\":\"NAV-PVT\"") != std::string::npos,
             "mixed export should include UBX runtime updates");
  ctx.Expect(jsonl.find("\"protocol\":\"UNICORE\"") != std::string::npos &&
                 jsonl.find("\"message\":\"BESTNAVA\"") != std::string::npos,
             "mixed export should include Unicore runtime updates");
  ctx.Expect(jsonl.find("\"protocol\":\"RTCM3\"") == std::string::npos,
             "RTCM metadata-only frames should not be exported as runtime samples");
}

void TestPrettyJsonlExport(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("nmea/basic_fix.nmea");
  const auto replay = universal_gnss_tools::ReplayGnssBytes(bytes, true);

  universal_gnss_tools::RuntimeExportOptions options;
  options.pretty = true;
  const std::string jsonl = universal_gnss_tools::FormatRuntimeExportJsonl(replay, options);
  const auto lines = SplitLines(jsonl);

  ctx.Expect(!lines.empty() &&
                 lines.front().find("\"event_index\": ") != std::string::npos &&
                 lines.front().find(", \"protocol\": ") != std::string::npos,
             "pretty JSONL should remain one object per line with stable spaced formatting");
}

void TestFileOutputHandling(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("nmea/basic_fix.nmea");
  const auto replay = universal_gnss_tools::ReplayGnssBytes(bytes, true);
  const std::string path = "/tmp/universal_gnss_runtime_export_test.jsonl";

  {
    std::ofstream output(path, std::ios::binary);
    ctx.Expect(output.is_open(), "test should be able to open a temporary export file");
    if (output.is_open())
    {
      const std::size_t lines_written =
          universal_gnss_tools::WriteRuntimeExportJsonl(output, replay);
      ctx.Expect(lines_written == 5u,
                 "file export helper should report the number of runtime samples written");
    }
  }

  std::ifstream input(path, std::ios::binary);
  ctx.Expect(input.is_open(), "exported JSONL file should be readable");
  if (!input.is_open())
  {
    return;
  }

  const std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
  const auto lines = SplitLines(content);
  ctx.Expect(lines.size() == 5u,
             "file export should persist one line per runtime sample");
  ctx.Expect(lines.front().find("\"message\":\"GGA\"") != std::string::npos,
             "file export should preserve stable schema keys and values");

  std::remove(path.c_str());
}

}  // namespace

int main()
{
  TestContext ctx;

  TestNmeaJsonlExport(ctx);
  TestMixedJsonlExport(ctx);
  TestPrettyJsonlExport(ctx);
  TestFileOutputHandling(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools runtime export tests passed\n";
  return EXIT_SUCCESS;
}
