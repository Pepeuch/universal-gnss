#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_discovery.hpp"
#include "universal_gnss_tools/receiver_discovery_format.hpp"

namespace
{

using universal_gnss_driver::ReceiverDetectedFamily;
using universal_gnss_driver::ReceiverPortSource;
using universal_gnss_driver::ReceiverProbeConfidence;
using universal_gnss_driver::ReceiverProbeResult;
using universal_gnss_tools::FormatReceiverDiscoveryJson;
using universal_gnss_tools::FormatReceiverDiscoveryText;

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

ReceiverProbeResult MakeResult(const std::string& path,
                               const ReceiverDetectedFamily family,
                               const ReceiverProbeConfidence confidence)
{
  ReceiverProbeResult result;
  result.path = path;
  result.source = ReceiverPortSource::kSerialById;
  result.stable_id = "stable";
  result.selected_baud = 921600u;
  result.detected_family = family;
  result.confidence = confidence;
  result.evidence.ubx_frames_seen = 2u;
  result.evidence.bytes_read = 512u;
  result.note = "ok";
  return result;
}

void TestTextFormatting(TestContext& ctx)
{
  const std::string text = FormatReceiverDiscoveryText(
      {MakeResult("/dev/serial/by-id/f9p", ReceiverDetectedFamily::kUblox,
                  ReceiverProbeConfidence::kHigh)});

  ctx.Expect(text.find("/dev/serial/by-id/f9p") != std::string::npos &&
                 text.find("baud=921600") != std::string::npos &&
                 text.find("family=ublox") != std::string::npos &&
                 text.find("confidence=high") != std::string::npos &&
                 text.find("evidence=ubx:2") != std::string::npos,
             "text discovery output should include path, baud, family, confidence, and evidence");
}

void TestJsonFormatting(TestContext& ctx)
{
  const std::string json = FormatReceiverDiscoveryJson(
      {MakeResult("/dev/ttyUSB0", ReceiverDetectedFamily::kUnicore,
                  ReceiverProbeConfidence::kHigh)});

  ctx.Expect(json.find("\"path\": \"/dev/ttyUSB0\"") != std::string::npos &&
                 json.find("\"detected_family\": \"unicore\"") != std::string::npos &&
                 json.find("\"confidence\": \"high\"") != std::string::npos &&
                 json.find("\"ubx_frames_seen\": 2") != std::string::npos,
             "JSON discovery output should include stable schema keys");
}

void TestEmptyFormatting(TestContext& ctx)
{
  const std::string text = FormatReceiverDiscoveryText({});
  const std::string json = FormatReceiverDiscoveryJson({});

  ctx.Expect(text == "No receiver candidates found\n",
             "empty text output should stay readable");
  ctx.Expect(json == "[\n]\n",
             "empty JSON output should still be a valid list");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestTextFormatting(ctx);
  TestJsonFormatting(ctx);
  TestEmptyFormatting(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_tools receiver discovery format tests passed\n";
  return EXIT_SUCCESS;
}
