#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "testdata_utils.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_tools/gnss_replay.hpp"
#include "universal_gnss_tools/runtime_export.hpp"

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

void AppendLittleEndian16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void AppendLittleEndian32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void WriteLittleEndian32(std::vector<std::uint8_t>& bytes,
                         const std::size_t offset,
                         const std::uint32_t value)
{
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void WriteLittleEndianFloat32(std::vector<std::uint8_t>& bytes,
                              const std::size_t offset,
                              const float value)
{
  std::uint32_t raw = 0u;
  std::memcpy(&raw, &value, sizeof(raw));
  WriteLittleEndian32(bytes, offset, raw);
}

void WriteLittleEndianFloat64(std::vector<std::uint8_t>& bytes,
                              const std::size_t offset,
                              const double value)
{
  std::uint64_t raw = 0u;
  std::memcpy(&raw, &value, sizeof(raw));
  bytes[offset] = static_cast<std::uint8_t>(raw & 0xFFu);
  bytes[offset + 1u] = static_cast<std::uint8_t>((raw >> 8) & 0xFFu);
  bytes[offset + 2u] = static_cast<std::uint8_t>((raw >> 16) & 0xFFu);
  bytes[offset + 3u] = static_cast<std::uint8_t>((raw >> 24) & 0xFFu);
  bytes[offset + 4u] = static_cast<std::uint8_t>((raw >> 32) & 0xFFu);
  bytes[offset + 5u] = static_cast<std::uint8_t>((raw >> 40) & 0xFFu);
  bytes[offset + 6u] = static_cast<std::uint8_t>((raw >> 48) & 0xFFu);
  bytes[offset + 7u] = static_cast<std::uint8_t>((raw >> 56) & 0xFFu);
}

std::vector<std::uint8_t> BuildUnicoreBinaryFrame(const std::uint16_t message_id,
                                                  const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> frame = {
      universal_gnss_protocols::kUnicoreBinarySync1,
      universal_gnss_protocols::kUnicoreBinarySync2,
      universal_gnss_protocols::kUnicoreBinarySync3,
      97u,
  };
  AppendLittleEndian16(frame, message_id);
  AppendLittleEndian16(frame, static_cast<std::uint16_t>(payload.size()));
  frame.push_back(0u);
  frame.push_back(1u);
  AppendLittleEndian16(frame, 2190u);
  AppendLittleEndian32(frame, 364536000u);
  AppendLittleEndian32(frame, 18u);
  frame.push_back(0u);
  frame.push_back(13u);
  AppendLittleEndian16(frame, 0u);
  frame.insert(frame.end(), payload.begin(), payload.end());

  const std::uint32_t crc =
      universal_gnss_protocols::ComputeUnicoreBinaryCrc32(frame.data(), frame.size());
  AppendLittleEndian32(frame, crc);
  return frame;
}

std::vector<std::uint8_t> MakeBestNavBinaryPayload()
{
  std::vector<std::uint8_t> payload(120u, 0u);
  WriteLittleEndian32(payload, 0u, 0u);
  WriteLittleEndian32(payload, 4u, 34u);
  WriteLittleEndianFloat64(payload, 8u, 40.0789588272);
  WriteLittleEndianFloat64(payload, 16u, 116.2365102982);
  WriteLittleEndianFloat64(payload, 24u, 65.8312);
  WriteLittleEndianFloat32(payload, 40u, 1.2221f);
  WriteLittleEndianFloat32(payload, 44u, 1.1053f);
  WriteLittleEndianFloat32(payload, 48u, 2.1970f);
  WriteLittleEndianFloat32(payload, 56u, 0.4f);
  payload[64u] = 50u;
  payload[65u] = 28u;
  return payload;
}

std::vector<std::uint8_t> MakePvtslnBinaryPayload()
{
  std::vector<std::uint8_t> payload(224u, 0u);
  WriteLittleEndian32(payload, 0u, 50u);
  WriteLittleEndianFloat32(payload, 4u, 60.5060f);
  WriteLittleEndianFloat64(payload, 8u, 40.07898130522);
  WriteLittleEndianFloat64(payload, 16u, 116.23663134427);
  WriteLittleEndianFloat32(payload, 24u, 0.2000f);
  WriteLittleEndianFloat32(payload, 28u, 0.1500f);
  WriteLittleEndianFloat32(payload, 32u, 0.1800f);
  WriteLittleEndianFloat32(payload, 36u, 0.9000f);
  WriteLittleEndian32(payload, 40u, 16u);
  WriteLittleEndianFloat32(payload, 44u, 60.5060f);
  WriteLittleEndianFloat64(payload, 48u, 40.07898130522);
  WriteLittleEndianFloat64(payload, 56u, 116.23663134427);
  WriteLittleEndianFloat32(payload, 64u, -8.4923f);
  payload[68u] = 46u;
  payload[69u] = 28u;
  payload[70u] = 46u;
  payload[71u] = 28u;
  WriteLittleEndianFloat64(payload, 72u, 0.0009);
  WriteLittleEndianFloat64(payload, 80u, -0.0031);
  WriteLittleEndianFloat64(payload, 88u, 0.0032);
  WriteLittleEndian32(payload, 96u, 0u);
  WriteLittleEndianFloat32(payload, 100u, 1.5000f);
  WriteLittleEndianFloat32(payload, 104u, 182.2500f);
  WriteLittleEndianFloat32(payload, 108u, 0.1000f);
  payload[112u] = 28u;
  payload[113u] = 25u;
  payload[114u] = 12u;
  payload[115u] = 8u;
  WriteLittleEndianFloat32(payload, 116u, 2.1753f);
  WriteLittleEndianFloat32(payload, 120u, 1.3480f);
  WriteLittleEndianFloat32(payload, 124u, 0.6840f);
  WriteLittleEndianFloat32(payload, 128u, 1.8392f);
  WriteLittleEndianFloat32(payload, 132u, 1.7072f);
  WriteLittleEndianFloat32(payload, 136u, 5.0f);
  payload[140u] = 28u;
  payload[141u] = 25u;
  payload[142u] = 26u;
  return payload;
}

void TestNmeaJsonlExport(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("nmea/basic_fix.nmea");
  const auto replay = universal_gnss_tools::ReplayGnssBytes(bytes, true);
  const std::string jsonl = universal_gnss_tools::FormatRuntimeExportJsonl(replay);
  const auto lines = SplitLines(jsonl);

  ctx.Expect(lines.size() == 5u, "basic NMEA export should emit one JSON line per runtime update");
  ctx.Expect(lines.front().front() == '{' && lines.front().back() == '}',
             "each JSONL export line should be a single JSON object");
  ctx.Expect(lines.front().find("{\"schema_version\":1,\"event_index\":1,") == 0u,
             "JSONL export should begin every record with the stable v1 schema marker");
  ctx.Expect(lines.front().find("\"protocol\":\"NMEA\"") != std::string::npos &&
                 lines.front().find("\"message\":\"GGA\"") != std::string::npos,
             "the first NMEA export line should identify the GGA update");
  ctx.Expect(lines.front().find("\"latitude_deg\":48.117300000") != std::string::npos &&
                 lines.front().find("\"longitude_deg\":11.516666667") != std::string::npos,
             "NMEA JSONL export should keep coordinate text precision above seven decimals");
  ctx.Expect(lines.back().find("\"message\":\"GST\"") != std::string::npos &&
                 lines.back().find("\"horizontal_accuracy_m\":0.6") != std::string::npos &&
                 lines.back().find("\"vertical_accuracy_m\":1.1") != std::string::npos,
             "the GST export line should carry conservative accuracy values");
  ctx.Expect(lines.front().find("\"satellites_tracked\":null") != std::string::npos &&
                 lines.front().find("\"dual_antenna_heading\":null") != std::string::npos &&
                 lines.front().find("\"dual_antenna_baseline\":null") != std::string::npos &&
                 lines.front().find("\"baseline_solution_status\":null") != std::string::npos &&
                 lines.front().find("\"interference_detected\":null") != std::string::npos,
             "missing optional fields should be exported as null consistently");
}

void TestMixedJsonlExport(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("mixed/nmea_ubx_rtcm_unicore.bin");
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

void TestUnicoreBinaryJsonlExport(TestContext& ctx)
{
  std::vector<std::uint8_t> bytes;
  const auto bestnav = BuildUnicoreBinaryFrame(2118u, MakeBestNavBinaryPayload());
  bytes.insert(bytes.end(), bestnav.begin(), bestnav.end());
  const auto pvtsln = BuildUnicoreBinaryFrame(1021u, MakePvtslnBinaryPayload());
  bytes.insert(bytes.end(), pvtsln.begin(), pvtsln.end());

  const auto replay = universal_gnss_tools::ReplayGnssBytes(bytes, true);
  const auto lines = SplitLines(universal_gnss_tools::FormatRuntimeExportJsonl(replay));

  ctx.Expect(lines.size() == 2u,
             "binary Unicore export should emit one line per routed runtime update");
  ctx.Expect(lines.front().find("\"protocol\":\"UNICORE\"") != std::string::npos &&
                 lines.front().find("\"message\":\"BESTNAVB\"") != std::string::npos,
             "binary Unicore export should expose BESTNAVB update names");
  ctx.Expect(lines.front().find("\"latitude_deg\":40.078958827") != std::string::npos &&
                 lines.front().find("\"longitude_deg\":116.236510298") != std::string::npos,
             "binary Unicore export should preserve high-precision coordinates in JSONL");
  ctx.Expect(lines.back().find("\"message\":\"PVTSLNB\"") != std::string::npos &&
                 lines.back().find("\"heading_deg\":182.25") != std::string::npos &&
                 lines.back().find("\"dual_antenna_baseline\":true") != std::string::npos &&
                 lines.back().find("\"baseline_azimuth_deg\":182.25") != std::string::npos &&
                 lines.back().find("\"baseline_pitch_deg\":0.1") != std::string::npos &&
                 lines.back().find("\"baseline_length_m\":1.5") != std::string::npos &&
                 lines.back().find("\"baseline_solution_status\":\"computed\"") !=
                     std::string::npos &&
                 lines.back().find("\"hdop\":0.684") != std::string::npos,
             "binary Unicore export should preserve routed PVTSLNB baseline, heading, and DOP fields");
}

void TestPrettyJsonlExport(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("nmea/basic_fix.nmea");
  const auto replay = universal_gnss_tools::ReplayGnssBytes(bytes, true);

  universal_gnss_tools::RuntimeExportOptions options;
  options.pretty = true;
  const std::string jsonl = universal_gnss_tools::FormatRuntimeExportJsonl(replay, options);
  const auto lines = SplitLines(jsonl);

  ctx.Expect(!lines.empty() && lines.front().find("\"schema_version\": 1, ") != std::string::npos &&
                 lines.front().find("\"event_index\": ") != std::string::npos &&
                 lines.front().find(", \"protocol\": ") != std::string::npos,
             "pretty JSONL should remain one object per line with stable spaced formatting");
}

void TestJsonlEscapingAndEmptyTimeline(TestContext& ctx)
{
  universal_gnss_tools::GnssReplayResult replay;
  ctx.Expect(universal_gnss_tools::FormatRuntimeExportJsonl(replay).empty(),
             "an empty runtime timeline should emit zero JSONL records");

  replay.events.resize(1u);
  auto& event = replay.events.front();
  event.event_index = 9u;
  event.protocol = universal_gnss_protocols::ProtocolType::kUnknown;
  event.identity = "value,\"quoted\"\nline";
  event.produced_runtime_update = true;
  const std::string jsonl = universal_gnss_tools::FormatRuntimeExportJsonl(replay);
  ctx.Expect(jsonl.find("\"schema_version\":1") != std::string::npos &&
                 jsonl.find("\"message\":\"value,\\\"quoted\\\"\\nline\"") !=
                     std::string::npos,
             "JSONL v1 should retain JSON escaping for message provenance");
}

void TestJsonlCsvRowSelectionConsistency(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("mixed/nmea_ubx_rtcm_unicore.bin");
  const auto replay = universal_gnss_tools::ReplayGnssBytes(bytes, true);
  const auto jsonl_lines = SplitLines(universal_gnss_tools::FormatRuntimeExportJsonl(replay));
  const auto csv_lines = SplitLines(universal_gnss_tools::FormatRuntimeExportCsv(replay));
  ctx.Expect(!csv_lines.empty() && jsonl_lines.size() + 1u == csv_lines.size(),
             "JSONL and CSV should select the same runtime-update events");
}

void TestCsvExport(TestContext& ctx)
{
  const auto bytes = universal_gnss_tools::test::ReadBinaryFile("nmea/basic_fix.nmea");
  const auto replay = universal_gnss_tools::ReplayGnssBytes(bytes, true);
  const auto lines = SplitLines(universal_gnss_tools::FormatRuntimeExportCsv(replay));

  constexpr char kExpectedHeader[] =
      "schema_version,event_index,timestamp_ns,protocol,message,fix_valid,fix_type,rtk_mode,"
      "latitude_deg,longitude_deg,altitude_m,horizontal_accuracy_m,vertical_accuracy_m,hdop,vdop,"
      "satellites_used,satellites_tracked,satellites_visible,mean_cn0_dbhz,max_cn0_dbhz,"
      "correction_age_s,heading_deg,dual_antenna_heading,dual_antenna_baseline,"
      "baseline_azimuth_deg,baseline_pitch_deg,baseline_length_m,baseline_solution_status,"
      "interference_detected,jamming_detected";
  ctx.Expect(lines.size() == 6u && lines.front() == kExpectedHeader,
             "CSV export should always emit the stable v1 header before runtime-update rows");
  ctx.Expect(lines.size() > 1u &&
                 lines[1].find("1,1,,NMEA,GGA,true,fix,none,48.117300000,11.516666667,") == 0u,
             "CSV export should preserve event order, missing timestamps, protocol provenance, and coordinate precision");
  ctx.Expect(lines.size() > 1u && lines[1].find(",,,,") != std::string::npos,
             "CSV export should represent unavailable optional values as empty cells");
  ctx.Expect(lines.back().find(",0.6,1.1,") != std::string::npos,
             "CSV export should preserve normalized runtime values without changing their meaning");
}

void TestCsvEscapingAndEmptyTimeline(TestContext& ctx)
{
  universal_gnss_tools::GnssReplayResult replay;
  ctx.Expect(universal_gnss_tools::FormatRuntimeExportCsv(replay) ==
                 "schema_version,event_index,timestamp_ns,protocol,message,fix_valid,fix_type,rtk_mode,"
                 "latitude_deg,longitude_deg,altitude_m,horizontal_accuracy_m,vertical_accuracy_m,hdop,vdop,"
                 "satellites_used,satellites_tracked,satellites_visible,mean_cn0_dbhz,max_cn0_dbhz,"
                 "correction_age_s,heading_deg,dual_antenna_heading,dual_antenna_baseline,"
                 "baseline_azimuth_deg,baseline_pitch_deg,baseline_length_m,baseline_solution_status,"
                 "interference_detected,jamming_detected\n",
             "an empty timeline should still produce the CSV v1 header");

  replay.events.resize(1u);
  auto& event = replay.events.front();
  event.event_index = 9u;
  event.protocol = universal_gnss_protocols::ProtocolType::kUnknown;
  event.identity = "value,\"quoted\"";
  event.produced_runtime_update = true;
  const auto lines = SplitLines(universal_gnss_tools::FormatRuntimeExportCsv(replay));
  ctx.Expect(lines.size() == 2u &&
                 lines.back().find(",UNKNOWN,\"value,\"\"quoted\"\"\",false,unknown,") !=
                     std::string::npos,
             "CSV export should use standard double-quote escaping for text fields");
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
  ctx.Expect(lines.size() == 5u, "file export should persist one line per runtime sample");
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
  TestUnicoreBinaryJsonlExport(ctx);
  TestPrettyJsonlExport(ctx);
  TestJsonlEscapingAndEmptyTimeline(ctx);
  TestJsonlCsvRowSelectionConsistency(ctx);
  TestCsvExport(ctx);
  TestCsvEscapingAndEmptyTimeline(ctx);
  TestFileOutputHandling(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools runtime export tests passed\n";
  return EXIT_SUCCESS;
}
