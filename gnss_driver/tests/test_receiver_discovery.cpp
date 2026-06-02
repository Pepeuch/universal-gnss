#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_discovery.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"

namespace
{

namespace fs = std::filesystem;

using universal_gnss_driver::DiscoverSerialPorts;
using universal_gnss_driver::MakeExplicitReceiverPortCandidate;
using universal_gnss_driver::ReceiverDetectedFamily;
using universal_gnss_driver::ReceiverDiscoveryPaths;
using universal_gnss_driver::ReceiverPortCandidate;
using universal_gnss_driver::ReceiverPortSource;
using universal_gnss_driver::ReceiverProbeConfidence;
using universal_gnss_driver::ReceiverProbeConfig;
using universal_gnss_driver::ReceiverProbeResult;
using universal_gnss_driver::SortReceiverProbeResults;

struct TestContext
{
  int failures{0};

  void Expect(bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

std::vector<std::uint8_t> BuildUbxFrame(const std::uint8_t class_id,
                                        const std::uint8_t message_id,
                                        const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> bytes;
  bytes.reserve(6u + payload.size() + 2u);
  bytes.push_back(0xB5u);
  bytes.push_back(0x62u);
  bytes.push_back(class_id);
  bytes.push_back(message_id);
  bytes.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((payload.size() >> 8) & 0xFFu));
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const auto checksum =
      universal_gnss_protocols::ComputeUbxChecksum(bytes.data() + 2u, bytes.size() - 2u);
  bytes.push_back(checksum.ck_a);
  bytes.push_back(checksum.ck_b);
  return bytes;
}

std::vector<std::uint8_t> BuildRtcmFrame(const std::uint16_t message_type)
{
  const std::vector<std::uint8_t> payload = {
      static_cast<std::uint8_t>((message_type >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((message_type & 0x0Fu) << 4u),
  };

  std::vector<std::uint8_t> bytes = {
      0xD3u,
      0x00u,
      static_cast<std::uint8_t>(payload.size()),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
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
  AppendLittleEndian16(frame, 2200u);
  AppendLittleEndian32(frame, 123456u);
  AppendLittleEndian32(frame, 18u);
  frame.push_back(0u);
  frame.push_back(16u);
  AppendLittleEndian16(frame, 0u);
  frame.insert(frame.end(), payload.begin(), payload.end());

  const std::uint32_t crc =
      universal_gnss_protocols::ComputeUnicoreBinaryCrc32(frame.data(), frame.size());
  AppendLittleEndian32(frame, crc);
  return frame;
}

void Append(std::vector<std::uint8_t>& destination, const std::vector<std::uint8_t>& source)
{
  destination.insert(destination.end(), source.begin(), source.end());
}

ReceiverProbeResult Analyze(const ReceiverPortCandidate& candidate,
                            const std::vector<std::uint8_t>& bytes,
                            const bool allow_nmea = false)
{
  ReceiverProbeConfig config;
  config.allow_generic_nmea_fallback = allow_nmea;
  return universal_gnss_driver::AnalyzeReceiverProbeBytes(candidate, 921600u, bytes, config);
}

void TestPortEnumerationPrefersByIdAndDeduplicates(TestContext& ctx)
{
  const fs::path root = fs::temp_directory_path() / "universal_gnss_discovery_test";
  fs::remove_all(root);
  fs::create_directories(root / "dev/serial/by-id");

  std::ofstream(root / "dev/ttyACM0").put('\n');
  std::ofstream(root / "dev/ttyUSB0").put('\n');
  fs::create_symlink(root / "dev/ttyACM0", root / "dev/serial/by-id/zed-f9p");

  ReceiverDiscoveryPaths paths;
  paths.serial_by_id_dir = (root / "dev/serial/by-id").string();
  paths.dev_dir = (root / "dev").string();

  const auto candidates = DiscoverSerialPorts(paths);
  ctx.Expect(candidates.size() == 2u,
             "enumeration should deduplicate by-id symlinks against tty device paths");
  ctx.Expect(candidates.front().source == ReceiverPortSource::kSerialById &&
                 candidates.front().stable_id == std::optional<std::string>("zed-f9p"),
             "enumeration should prefer stable /dev/serial/by-id paths first");
  ctx.Expect(candidates.front().path.find("/serial/by-id/zed-f9p") != std::string::npos,
             "by-id candidate should preserve its stable symlink path");
  ctx.Expect(candidates.back().source == ReceiverPortSource::kTtyUsb &&
                 candidates.back().path.find("ttyUSB0") != std::string::npos,
             "enumeration should still include unmatched ttyUSB devices");

  fs::remove_all(root);
}

void TestExplicitPathCandidate(TestContext& ctx)
{
  const auto candidate =
      MakeExplicitReceiverPortCandidate("/dev/ttyUSB42", ReceiverDiscoveryPaths{});
  ctx.Expect(candidate.source == ReceiverPortSource::kExplicitPath &&
                 candidate.path == "/dev/ttyUSB42",
             "explicit-path probing should build a direct probe candidate");
}

void TestUbxDetection(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyACM0";

  std::vector<std::uint8_t> bytes = {0x01u, 0x02u};
  Append(bytes, BuildUbxFrame(0x01u, 0x07u, std::vector<std::uint8_t>(92u, 0u)));
  const auto result = Analyze(candidate, bytes);

  ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUblox &&
                 result.confidence == ReceiverProbeConfidence::kHigh &&
                 result.evidence.ubx_frames_seen == 1u,
             "valid UBX frames should detect a high-confidence u-blox receiver");
}

void TestUnicoreAsciiDetection(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB0";

  const std::string line =
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,"
      "WGS84,1.2221,1.1053,2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,"
      "SOL_COMPUTED,DOPPLER_VELOCITY,0.000,0.000,0.0046,335.592288,0.0045,"
      "0.0194,0.0123*c1b4f7fe\r\n";
  const auto result = Analyze(
      candidate, std::vector<std::uint8_t>(line.begin(), line.end()));

  ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUnicore &&
                 result.confidence == ReceiverProbeConfidence::kHigh &&
                 result.evidence.unicore_ascii_seen == 1u,
             "clear Unicore ASCII runtime messages should detect Unicore with high confidence");
}

void TestUnicoreBinaryDetection(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB0";

  const auto bytes = BuildUnicoreBinaryFrame(2118u, std::vector<std::uint8_t>(120u, 0u));
  const auto result = Analyze(candidate, bytes);

  ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUnicore &&
                 result.confidence == ReceiverProbeConfidence::kHigh &&
                 result.evidence.unicore_binary_seen == 1u,
             "valid Unicore N4 frames should detect Unicore with high confidence");
}

void TestNmeaFallbackPolicy(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB1";

  const std::string sentence = "$GPGLL,4916.45,N,12311.12,W,225444,A,*1D\r\n";
  const std::vector<std::uint8_t> bytes(sentence.begin(), sentence.end());

  const auto disabled = Analyze(candidate, bytes, false);
  ctx.Expect(disabled.detected_family == ReceiverDetectedFamily::kUnknown &&
                 disabled.confidence == ReceiverProbeConfidence::kLow &&
                 disabled.note == "nmea_only_fallback_disabled",
             "NMEA-only probing should stay unknown when generic NMEA fallback is disabled");

  const auto enabled = Analyze(candidate, bytes, true);
  ctx.Expect(enabled.detected_family == ReceiverDetectedFamily::kNmea &&
                 enabled.confidence == ReceiverProbeConfidence::kMedium &&
                 enabled.evidence.nmea_sentences_seen == 1u,
             "NMEA-only probing should become medium-confidence generic NMEA when enabled");
}

void TestUnknownAndRtcmOnlyStreams(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB2";

  const auto unknown = Analyze(candidate, std::vector<std::uint8_t>{0x10u, 0x20u, 0x30u});
  ctx.Expect(unknown.detected_family == ReceiverDetectedFamily::kUnknown &&
                 unknown.confidence == ReceiverProbeConfidence::kNone,
             "noise-only streams should stay unknown with no confidence");

  const auto rtcm_only = Analyze(candidate, BuildRtcmFrame(1005u));
  ctx.Expect(rtcm_only.detected_family == ReceiverDetectedFamily::kUnknown &&
                 rtcm_only.confidence == ReceiverProbeConfidence::kLow &&
                 rtcm_only.note == "rtcm_only_stream",
             "RTCM-only streams should not claim a receiver family");
}

void TestResultOrdering(TestContext& ctx)
{
  ReceiverProbeResult unknown;
  unknown.path = "/dev/ttyUSB9";
  unknown.source = ReceiverPortSource::kTtyUsb;
  unknown.confidence = ReceiverProbeConfidence::kNone;

  ReceiverProbeResult nmea;
  nmea.path = "/dev/ttyACM0";
  nmea.source = ReceiverPortSource::kTtyAcm;
  nmea.detected_family = ReceiverDetectedFamily::kNmea;
  nmea.confidence = ReceiverProbeConfidence::kMedium;
  nmea.evidence.bytes_read = 128u;

  ReceiverProbeResult ublox;
  ublox.path = "/dev/serial/by-id/zed";
  ublox.source = ReceiverPortSource::kSerialById;
  ublox.detected_family = ReceiverDetectedFamily::kUblox;
  ublox.confidence = ReceiverProbeConfidence::kHigh;
  ublox.evidence.bytes_read = 256u;

  auto results = SortReceiverProbeResults({unknown, nmea, ublox});
  ctx.Expect(results.size() == 3u &&
                 results[0].detected_family == ReceiverDetectedFamily::kUblox &&
                 results[1].detected_family == ReceiverDetectedFamily::kNmea &&
                 results[2].detected_family == ReceiverDetectedFamily::kUnknown,
             "sorted probe results should keep the best-confidence result first");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestPortEnumerationPrefersByIdAndDeduplicates(ctx);
  TestExplicitPathCandidate(ctx);
  TestUbxDetection(ctx);
  TestUnicoreAsciiDetection(ctx);
  TestUnicoreBinaryDetection(ctx);
  TestNmeaFallbackPolicy(ctx);
  TestUnknownAndRtcmOnlyStreams(ctx);
  TestResultOrdering(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver discovery tests passed\n";
  return EXIT_SUCCESS;
}
