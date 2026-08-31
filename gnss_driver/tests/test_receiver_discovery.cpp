#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_discovery.hpp"
#include "universal_gnss_driver/stream_detector.hpp"
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
using universal_gnss_driver::DetectedStreamProtocol;
using universal_gnss_driver::StreamDetector;

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

std::vector<std::uint8_t> BuildMonVerPayload(const std::vector<std::string>& extensions)
{
  constexpr std::size_t kFixedPayloadSize = 40u;
  constexpr std::size_t kExtensionSize = 30u;
  std::vector<std::uint8_t> payload(kFixedPayloadSize + extensions.size() * kExtensionSize, 0u);

  const auto write_field = [&](const std::size_t offset,
                               const std::size_t size,
                               const std::string& text) {
    for (std::size_t index = 0u; index < text.size() && index + 1u < size; ++index)
    {
      payload[offset + index] = static_cast<std::uint8_t>(text[index]);
    }
  };

  write_field(0u, 30u, "EXT HPG 1.32");
  write_field(30u, 10u, "00080000");
  for (std::size_t index = 0u; index < extensions.size(); ++index)
  {
    write_field(kFixedPayloadSize + index * kExtensionSize, kExtensionSize, extensions[index]);
  }

  return payload;
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

std::vector<std::uint8_t> BuildMavlinkV1Heartbeat()
{
  std::vector<std::uint8_t> bytes = {
      0xFEu,
      9u,
      1u,
      1u,
      1u,
      0u,
      0u,
      0u,
      0u,
      0u,
      0u,
      0u,
      0u,
      0u,
      0u,
      0x12u,
      0x34u,
  };
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

std::string BuildUnicoreAsciiFrame(const std::string& frame_without_crc)
{
  const std::uint32_t crc = universal_gnss_protocols::ComputeUnicoreBinaryCrc32(
      reinterpret_cast<const std::uint8_t*>(frame_without_crc.data() + 1u),
      frame_without_crc.size() - 1u);
  std::ostringstream stream;
  stream << frame_without_crc << '*' << std::hex << std::setfill('0') << std::setw(8) << crc
         << "\r\n";
  return stream.str();
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

void TestPlatformUartsExcludedByDefault(TestContext& ctx)
{
  const fs::path root =
      fs::temp_directory_path() / "universal_gnss_discovery_platform_default_test";
  fs::remove_all(root);
  fs::create_directories(root / "dev/serial/by-id");

  std::ofstream(root / "dev/ttyAMA0").put('\n');
  std::ofstream(root / "dev/ttyS1").put('\n');
  std::ofstream(root / "dev/ttyTHS2").put('\n');

  ReceiverDiscoveryPaths paths;
  paths.serial_by_id_dir = (root / "dev/serial/by-id").string();
  paths.dev_dir = (root / "dev").string();

  const auto candidates = DiscoverSerialPorts(paths);
  ctx.Expect(candidates.empty(),
             "platform UARTs should stay excluded unless explicitly enabled");

  fs::remove_all(root);
}

void TestPlatformUartsIncludedAndDeduplicated(TestContext& ctx)
{
  const fs::path root =
      fs::temp_directory_path() / "universal_gnss_discovery_platform_enabled_test";
  fs::remove_all(root);
  fs::create_directories(root / "dev/serial/by-id");

  std::ofstream(root / "dev/ttyUSB0").put('\n');
  std::ofstream(root / "dev/ttyAMA0").put('\n');
  std::ofstream(root / "dev/ttyAMA2").put('\n');
  std::ofstream(root / "dev/ttyS1").put('\n');
  std::ofstream(root / "dev/ttyTHS2").put('\n');
  fs::create_symlink(root / "dev/ttyAMA0", root / "dev/serial0");

  ReceiverDiscoveryPaths paths;
  paths.serial_by_id_dir = (root / "dev/serial/by-id").string();
  paths.dev_dir = (root / "dev").string();

  ReceiverProbeConfig config;
  config.include_platform_uarts = true;
  config.platform_uart_paths = {
      (root / "dev/serial0").string(),
      (root / "dev/serial1").string(),
  };

  const auto candidates = DiscoverSerialPorts(config, paths);
  ctx.Expect(candidates.size() == 5u,
             "enabled platform UART scanning should include aliases and prefixed UARTs with dedup");
  ctx.Expect(candidates[0].source == ReceiverPortSource::kTtyUsb &&
                 candidates[0].path.find("ttyUSB0") != std::string::npos,
             "existing USB enumeration priority should remain unchanged");
  ctx.Expect(candidates[1].source == ReceiverPortSource::kPlatformUart &&
                 candidates[1].path.find("serial0") != std::string::npos,
             "platform UART aliases should be preferred over their canonical tty target");
  ctx.Expect(candidates[2].source == ReceiverPortSource::kPlatformUart &&
                 candidates[2].path.find("ttyAMA2") != std::string::npos,
             "ttyAMA devices should be included when platform UART scanning is enabled");
  ctx.Expect(candidates[3].source == ReceiverPortSource::kPlatformUart &&
                 candidates[3].path.find("ttyS1") != std::string::npos,
             "ttyS devices should be included deterministically after ttyAMA");
  ctx.Expect(candidates[4].source == ReceiverPortSource::kPlatformUart &&
                 candidates[4].path.find("ttyTHS2") != std::string::npos,
             "ttyTHS devices should be included when platform UART scanning is enabled");

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
                 result.discovery_score == 100 &&
                 result.evidence.ubx_frames_seen == 1u && !result.identity.model.has_value() &&
                 !result.identity.firmware_version.has_value() &&
                 result.reason.find("valid_ubx_frame:+100") != std::string::npos,
             "UBX traffic without MON-VER should detect u-blox without inventing metadata");
}

void TestUbloxMonVerMetadata(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyACM0";

  const auto metadata_bytes = BuildUbxFrame(
      0x0Au,
      0x04u,
      BuildMonVerPayload({"MOD=ZED-F9P-00B", "FWVER=HPG 1.32", "CHIPID=000000D0D69D0F7A54"}));
  const auto result = Analyze(candidate, metadata_bytes);
  const auto identity_only = Analyze(
      candidate, BuildUbxFrame(0x0Au, 0x04u, BuildMonVerPayload({"CHIPID=000000D0D69D0F7A54"})));
  const auto replacement = Analyze(
      candidate, BuildUbxFrame(0x01u, 0x07u, std::vector<std::uint8_t>(92u, 0u)));

  ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUblox &&
                 result.identity.model == std::optional<std::string>{"ZED-F9P-00B"} &&
                 result.identity.firmware_version == std::optional<std::string>{"HPG 1.32"} &&
                 result.identity.receiver_identity ==
                     std::optional<std::string>{"000000D0D69D0F7A54"},
             "valid MON-VER extensions should provide documented u-blox model, firmware, and chip identity");
  ctx.Expect(identity_only.identity.receiver_identity ==
                     std::optional<std::string>{"000000D0D69D0F7A54"} &&
                 !identity_only.identity.model.has_value() &&
                 !identity_only.identity.firmware_version.has_value(),
             "a documented CHIPID-only MON-VER reply should retain only observed receiver identity");
  ctx.Expect(!replacement.identity.receiver_identity.has_value() &&
                 !replacement.identity.model.has_value() &&
                 !replacement.identity.firmware_version.has_value(),
             "a replacement probe without MON-VER must not retain prior u-blox metadata");
}

void TestUbloxMonVerRejectsMalformedPayload(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyACM0";

  auto malformed_payload = BuildMonVerPayload(
      {"MOD=ZED-F9P-00B", "CHIPID=000000D0D69D0F7A54", "FWVER=HPG 1.32"});
  for (std::size_t index = 100u; index < 130u; ++index)
  {
    malformed_payload[index] = static_cast<std::uint8_t>('X');
  }
  std::vector<std::uint8_t> bytes = BuildUbxFrame(0x01u, 0x07u, std::vector<std::uint8_t>(92u, 0u));
  Append(bytes, BuildUbxFrame(0x0Au, 0x04u, malformed_payload));
  const auto result = Analyze(candidate, bytes);

  ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUblox &&
                 !result.identity.receiver_identity.has_value() &&
                 !result.identity.model.has_value() &&
                 !result.identity.firmware_version.has_value(),
             "malformed MON-VER payloads must not become authoritative receiver metadata");
}

void TestUnicoreAsciiDetection(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB0";

  const std::string line = BuildUnicoreAsciiFrame(
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,"
      "WGS84,1.2221,1.1053,2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,"
      "SOL_COMPUTED,DOPPLER_VELOCITY,0.000,0.000,0.0046,335.592288,0.0045,"
      "0.0194,0.0123") +
      BuildUnicoreAsciiFrame(
          "#VERSIONA,94,GPS,FINE,2190,117325000,0,0,18,160;\"UM982\",\"R4.10Build5251\","
          "\"HRPT00-S10C-P\",\"2310415000012-LR23A2225208904\",\"ffff48ffff0fffff\","
          "\"2021/11/26\"");
  const auto result = Analyze(
      candidate, std::vector<std::uint8_t>(line.begin(), line.end()));

  ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUnicore &&
                 result.confidence == ReceiverProbeConfidence::kHigh &&
                 result.discovery_score == 100 &&
                 result.evidence.unicore_ascii_seen == 1u &&
                 result.identity.model == std::optional<std::string>{"UM982"} &&
                 result.identity.receiver_identity ==
                     std::optional<std::string>{"2310415000012-LR23A2225208904"} &&
                 result.identity.firmware_version ==
                     std::optional<std::string>{"R4.10Build5251"},
             "verified Unicore probe data should retain documented VERSIONA model, firmware, and product serial identity");
}

void TestUnicoreVersionARequiresDocumentedFields(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB0";

  const std::string bytes = BuildUnicoreAsciiFrame(
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,"
      "WGS84,1.2221,1.1053,2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,"
      "SOL_COMPUTED,DOPPLER_VELOCITY,0.000,0.000,0.0046,335.592288,0.0045,"
      "0.0194,0.0123") +
      BuildUnicoreAsciiFrame(
          "#VERSIONA,94,GPS,FINE,2190,117325000,0,0,18,160;\"UM982\",R4.10Build5251");
  const auto result = Analyze(candidate, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));

  ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUnicore &&
                 !result.identity.receiver_identity.has_value() &&
                 !result.identity.model.has_value() &&
                 !result.identity.firmware_version.has_value(),
             "malformed VERSIONA fields must not become authoritative receiver metadata");
}

void TestUnicoreAsciiDiscoveryRequiresVerifiedPlausibleEvidence(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB0";
  const StreamDetector detector;

  const std::string valid_bestnav = BuildUnicoreAsciiFrame(
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,"
      "WGS84,1.2221,1.1053,2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,");
  const std::string crc_valid_malformed = BuildUnicoreAsciiFrame("#BESTNAVA,garbage");
  const std::string crc_invalid = valid_bestnav.substr(0u, valid_bestnav.size() - 10u) +
                                  "00000000\r\n";

  const std::vector<std::string> unverified_or_malformed = {
      "#BESTNAVA,garbage\r\n",
      crc_invalid,
      crc_valid_malformed,
      "#BESTNAVA,97,GPS,FINE,2294",
      "unrelated log #BESTNAVA,garbage\r\n",
  };

  for (const auto& input : unverified_or_malformed)
  {
    const auto bytes = std::vector<std::uint8_t>(input.begin(), input.end());
    const auto detected = detector.Detect(bytes);
    const auto result = Analyze(candidate, bytes);
    ctx.Expect(detected.protocol == DetectedStreamProtocol::kUnknown,
               "unverified or malformed Unicore-looking text must not select the Unicore stream detector");
    ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUnknown &&
                   result.confidence == ReceiverProbeConfidence::kNone &&
                   result.evidence.unicore_ascii_seen == 0u,
               "unverified or malformed Unicore-looking text must not produce high-confidence discovery");
  }

  const auto valid_bytes = std::vector<std::uint8_t>(valid_bestnav.begin(), valid_bestnav.end());
  const auto valid_detected = detector.Detect(valid_bytes);
  const auto valid_result = Analyze(candidate, valid_bytes);
  ctx.Expect(valid_detected.protocol == DetectedStreamProtocol::kUnicoreAscii &&
                 valid_result.detected_family == ReceiverDetectedFamily::kUnicore &&
                 valid_result.confidence == ReceiverProbeConfidence::kHigh &&
                 valid_result.evidence.unicore_ascii_seen == 1u,
             "CRC-valid, semantically plausible BESTNAVA must retain high-confidence Unicore discovery");

  std::string mixed = "noise #BESTNAVA,garbage\r\n";
  mixed += valid_bestnav;
  const auto mixed_bytes = std::vector<std::uint8_t>(mixed.begin(), mixed.end());
  const auto mixed_detected = detector.Detect(mixed_bytes);
  const auto mixed_result = Analyze(candidate, mixed_bytes);
  ctx.Expect(mixed_detected.protocol == DetectedStreamProtocol::kUnicoreAscii &&
                 mixed_result.detected_family == ReceiverDetectedFamily::kUnicore &&
                 mixed_result.confidence == ReceiverProbeConfidence::kHigh &&
                 mixed_result.evidence.unicore_ascii_seen == 1u,
             "verified Unicore evidence must still win after an earlier noisy supported-name token");
}

void TestUnicorePvtslnAndRtkStatusScoring(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB0";

  const std::string lines = BuildUnicoreAsciiFrame(
      "#RTKSTATUSA,97,GPS,FINE,2190,365354000,0,0,18,1;"
      "0,0,0,0,0,0,0,0,0,0,0,NARROW_INT,5,0,1,12,0") +
      BuildUnicoreAsciiFrame(
      "#PVTSLNA,97,GPS,FINE,2190,364536000,0,0,18,13;"
      "NARROW_INT,60.5060,40.07898130522,116.23663134427,0.2000,0.1500,0.1800,0.9000,"
      "SINGLE,60.5060,40.07898130522,116.23663134427,4.3353,46,28,46,28,0.0009,-0.0031,-0.0032,"
      "SOL_COMPUTED,1.5000,182.2500,0.1000,28,25,12,8,2.1753,1.3480,0.6840,1.8392,1.7072,5.0,"
      "28,25,26");
  const auto result = Analyze(
      candidate, std::vector<std::uint8_t>(lines.begin(), lines.end()));

  ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUnicore &&
                 result.confidence == ReceiverProbeConfidence::kHigh &&
                 result.discovery_score == 200 &&
                 result.reason.find("RTKSTATUSA:+100") != std::string::npos &&
                 result.reason.find("PVTSLNA:+100") != std::string::npos,
             "UM982 RTKSTATUSA/PVTSLNA replay should produce additive Unicore score reasons");
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

  const std::string sentence =
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
  const std::vector<std::uint8_t> bytes(sentence.begin(), sentence.end());

  const auto disabled = Analyze(candidate, bytes, false);
  ctx.Expect(disabled.detected_family == ReceiverDetectedFamily::kUnknown &&
                 disabled.confidence == ReceiverProbeConfidence::kLow &&
                 disabled.note == "nmea_only_fallback_disabled",
             "NMEA-only probing should stay unknown when generic NMEA fallback is disabled");

  const auto enabled = Analyze(candidate, bytes, true);
  ctx.Expect(enabled.detected_family == ReceiverDetectedFamily::kNmea &&
                 enabled.confidence == ReceiverProbeConfidence::kMedium &&
                 enabled.discovery_score == 20 &&
                 enabled.evidence.nmea_sentences_seen == 1u && !enabled.identity.model.has_value() &&
                 !enabled.identity.receiver_identity.has_value() &&
                 !enabled.identity.firmware_version.has_value(),
             "NMEA-only probing should remain metadata-unavailable rather than inventing receiver identity");
}

void TestNmeaFallbackRejectsNonRuntimeSentences(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB1";

  const std::string proprietary_or_text = "$GPTXT,01,01,02,u-blox ag - www.u-blox.com*50\r\n";
  const std::vector<std::uint8_t> bytes(
      proprietary_or_text.begin(), proprietary_or_text.end());

  const auto result = Analyze(candidate, bytes, true);
  ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUnknown &&
                 result.confidence == ReceiverProbeConfidence::kNone &&
                 result.evidence.nmea_sentences_seen == 0u,
             "generic NMEA fallback should ignore non-runtime GNSS sentences");
}

void TestMavlinkAndGarbageRejected(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB3";

  const auto mavlink = Analyze(candidate, BuildMavlinkV1Heartbeat(), true);
  ctx.Expect(mavlink.detected_family == ReceiverDetectedFamily::kUnknown &&
                 mavlink.confidence == ReceiverProbeConfidence::kNone &&
                 mavlink.discovery_score == -200 &&
                 mavlink.evidence.mavlink_heartbeats_seen == 1u &&
                 mavlink.note == "mavlink_heartbeat_detected",
             "MAVLink heartbeat streams should be explicitly rejected");

  const std::string garbage = "boot log: not a gnss receiver\nrandom serial text\n";
  const auto text = Analyze(
      candidate, std::vector<std::uint8_t>(garbage.begin(), garbage.end()), true);
  ctx.Expect(text.detected_family == ReceiverDetectedFamily::kUnknown &&
                 text.confidence == ReceiverProbeConfidence::kNone &&
                 text.discovery_score == -50 &&
                 text.evidence.random_ascii_bytes_seen == garbage.size() &&
                 text.note == "random_ascii_text",
             "random serial text should be penalized and rejected");
}

void TestSilentProbeRejected(TestContext& ctx)
{
  ReceiverPortCandidate candidate;
  candidate.path = "/dev/ttyUSB4";

  const auto result = Analyze(candidate, {}, true);
  ctx.Expect(result.detected_family == ReceiverDetectedFamily::kUnknown &&
                 result.confidence == ReceiverProbeConfidence::kNone &&
                 result.discovery_score == 0 &&
                 result.note == "no_data" &&
                 result.reason == "no_data",
             "silent ports should report no_data with no confidence");
}

void TestDefaultBaudOrder(TestContext& ctx)
{
  ReceiverProbeConfig config;
  const std::vector<std::uint32_t> expected = {
      921600u, 460800u, 230400u, 115200u, 38400u, 9600u};
  ctx.Expect(config.baud_candidates == expected,
             "default auto-baud list should match Auto Discovery v2 order");
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
  TestPlatformUartsExcludedByDefault(ctx);
  TestPlatformUartsIncludedAndDeduplicated(ctx);
  TestExplicitPathCandidate(ctx);
  TestUbxDetection(ctx);
  TestUbloxMonVerMetadata(ctx);
  TestUbloxMonVerRejectsMalformedPayload(ctx);
  TestUnicoreAsciiDetection(ctx);
  TestUnicoreVersionARequiresDocumentedFields(ctx);
  TestUnicoreAsciiDiscoveryRequiresVerifiedPlausibleEvidence(ctx);
  TestUnicorePvtslnAndRtkStatusScoring(ctx);
  TestUnicoreBinaryDetection(ctx);
  TestNmeaFallbackPolicy(ctx);
  TestNmeaFallbackRejectsNonRuntimeSentences(ctx);
  TestMavlinkAndGarbageRejected(ctx);
  TestSilentProbeRejected(ctx);
  TestDefaultBaudOrder(ctx);
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
