#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_driver/protocol_support.hpp"
#include "universal_gnss_driver/receiver_capabilities.hpp"
#include "universal_gnss_driver/receiver_profiles.hpp"
#include "universal_gnss_driver/stream_detector.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"

namespace
{

using universal_gnss_driver::DetectedStreamProtocol;
using universal_gnss_driver::ReceiverFeature;
using universal_gnss_driver::ReceiverProfile;
using universal_gnss_driver::ReceiverProtocol;
using universal_gnss_driver::StreamDetector;

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

std::vector<std::uint8_t> ToBytes(const std::string& text)
{
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

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

const ReceiverProfile& RequireProfile(TestContext& ctx, const std::string& profile_id)
{
  const ReceiverProfile* profile =
      universal_gnss_driver::FindBuiltInReceiverProfile(profile_id);
  ctx.Expect(profile != nullptr, "expected built-in receiver profile: " + profile_id);
  if (profile == nullptr)
  {
    std::cerr << "FAILED: missing required test profile, aborting\n";
    std::exit(EXIT_FAILURE);
  }

  return *profile;
}

void TestProtocolAndFeatureFlags(TestContext& ctx)
{
  universal_gnss_driver::ReceiverCapabilities capabilities;
  universal_gnss_driver::AddSupportedInputProtocol(capabilities, ReceiverProtocol::kUbx);
  universal_gnss_driver::AddSupportedOutputProtocol(capabilities, ReceiverProtocol::kNmea);
  universal_gnss_driver::AddSupportedInputProtocol(capabilities, ReceiverProtocol::kRtcm3);
  universal_gnss_driver::AddReceiverFeature(capabilities, ReceiverFeature::kRtk);
  universal_gnss_driver::AddReceiverFeature(capabilities, ReceiverFeature::kRoverMode);

  ctx.Expect(universal_gnss_driver::SupportsInputProtocol(capabilities, ReceiverProtocol::kUbx),
             "receiver capabilities should track supported UBX input");
  ctx.Expect(universal_gnss_driver::SupportsInputProtocol(capabilities, ReceiverProtocol::kRtcm3),
             "receiver capabilities should track supported RTCM input");
  ctx.Expect(universal_gnss_driver::SupportsOutputProtocol(capabilities, ReceiverProtocol::kNmea),
             "receiver capabilities should track supported NMEA output");
  ctx.Expect(universal_gnss_driver::HasReceiverFeature(capabilities, ReceiverFeature::kRtk) &&
                 universal_gnss_driver::HasReceiverFeature(
                     capabilities, ReceiverFeature::kRoverMode),
             "receiver capabilities should track receiver feature flags");
  ctx.Expect(!universal_gnss_driver::HasReceiverFeature(
                 capabilities, ReceiverFeature::kDualAntennaBaseline),
             "receiver capabilities should not invent unsupported baseline features");
  ctx.Expect(!universal_gnss_driver::SupportsOutputProtocol(
                 capabilities, ReceiverProtocol::kUbx),
             "receiver capabilities should not imply unsupported outputs");
}

void TestProfiles(TestContext& ctx)
{
  const auto& profiles = universal_gnss_driver::GetBuiltInReceiverProfiles();
  ctx.Expect(profiles.size() == 4u, "built-in receiver profile set should contain four profiles");

  const ReceiverProfile& generic = RequireProfile(ctx, "generic_nmea");
  ctx.Expect(universal_gnss_driver::SupportsOutputProtocol(
                 generic.capabilities, ReceiverProtocol::kNmea) &&
                 !universal_gnss_driver::SupportsInputProtocol(
                     generic.capabilities, ReceiverProtocol::kRtcm3),
             "generic NMEA profile should stay output-only and correction-agnostic");
  ctx.Expect(universal_gnss_driver::HasReceiverFeature(
                 generic.capabilities, ReceiverFeature::kRtk) &&
                 !universal_gnss_driver::HasReceiverFeature(
                     generic.capabilities, ReceiverFeature::kBaseMode),
             "generic NMEA profile should advertise RTK read visibility without implying base-mode support");

  const ReceiverProfile& ublox = RequireProfile(ctx, "ublox_f9_f10");
  ctx.Expect(universal_gnss_driver::SupportsInputProtocol(
                 ublox.capabilities, ReceiverProtocol::kUbx) &&
                 universal_gnss_driver::SupportsInputProtocol(
                     ublox.capabilities, ReceiverProtocol::kRtcm3) &&
                 universal_gnss_driver::SupportsOutputProtocol(
                     ublox.capabilities, ReceiverProtocol::kNmea),
             "u-blox family profile should advertise UBX, RTCM input, and NMEA output");
  ctx.Expect(universal_gnss_driver::HasReceiverFeature(
                 ublox.capabilities, ReceiverFeature::kPps) &&
                 universal_gnss_driver::HasReceiverFeature(
                     ublox.capabilities, ReceiverFeature::kRfMonitoring) &&
                 !universal_gnss_driver::HasReceiverFeature(
                     ublox.capabilities, ReceiverFeature::kDualAntennaBaseline) &&
                 !universal_gnss_driver::HasReceiverFeature(
                     ublox.capabilities, ReceiverFeature::kDualAntenna) &&
                 !universal_gnss_driver::HasReceiverFeature(
                     ublox.capabilities, ReceiverFeature::kSurveyIn),
             "u-blox family profile should stay conservative about non-universal features");

  const ReceiverProfile& unicore = RequireProfile(ctx, "unicore_um98x_placeholder");
  ctx.Expect(unicore.placeholder &&
                 universal_gnss_driver::SupportsInputProtocol(
                     unicore.capabilities, ReceiverProtocol::kUnicoreAscii) &&
                 universal_gnss_driver::HasReceiverFeature(
                     unicore.capabilities, ReceiverFeature::kDualAntennaBaseline) &&
                 universal_gnss_driver::HasReceiverFeature(
                     unicore.capabilities, ReceiverFeature::kDualAntenna),
             "Unicore placeholder should expose expected high-level placeholder capabilities");

  const ReceiverProfile& quectel = RequireProfile(ctx, "quectel_placeholder");
  ctx.Expect(quectel.placeholder &&
                 universal_gnss_driver::HasReceiverFeature(
                     quectel.capabilities, ReceiverFeature::kRtk) &&
                 !universal_gnss_driver::HasReceiverFeature(
                     quectel.capabilities, ReceiverFeature::kHeading) &&
                 !universal_gnss_driver::SupportsOutputProtocol(
                     quectel.capabilities, ReceiverProtocol::kUbx),
             "Quectel placeholder should not imply unsupported heading or UBX output");
}

void TestStreamDetection(TestContext& ctx)
{
  const StreamDetector detector;

  std::vector<std::uint8_t> nmea_stream = {0x00u, 0x7Fu};
  Append(nmea_stream, ToBytes("$GPGLL,4916.45,N,12311.12,W,225444,A,*1D\r\n"));
  const auto nmea_result = detector.Detect(nmea_stream);
  ctx.Expect(nmea_result.protocol == DetectedStreamProtocol::kNmea &&
                 nmea_result.bytes_consumed > 0u,
             "stream detector should classify a valid NMEA sentence");

  std::vector<std::uint8_t> ubx_stream = {0x01u, 0x02u, 0x03u};
  Append(ubx_stream, BuildUbxFrame(0x01u, 0x03u, std::vector<std::uint8_t>(16u, 0u)));
  const auto ubx_result = detector.Detect(ubx_stream);
  ctx.Expect(ubx_result.protocol == DetectedStreamProtocol::kUbx &&
                 ubx_result.frame_length_bytes == 24u,
             "stream detector should classify a valid UBX frame");

  std::vector<std::uint8_t> rtcm_stream = {0xFFu};
  Append(rtcm_stream, BuildRtcmFrame(1005u));
  const auto rtcm_result = detector.Detect(rtcm_stream);
  ctx.Expect(rtcm_result.protocol == DetectedStreamProtocol::kRtcm3 &&
                 rtcm_result.frame_length_bytes == 8u,
             "stream detector should classify a valid RTCM3 frame");

  std::vector<std::uint8_t> unicore_binary_stream = {0xAAu};
  Append(unicore_binary_stream, BuildUnicoreBinaryFrame(2118u, std::vector<std::uint8_t>(120u, 0u)));
  const auto unicore_binary_result = detector.Detect(unicore_binary_stream);
  ctx.Expect(unicore_binary_result.protocol == DetectedStreamProtocol::kUnicoreBinary &&
                 unicore_binary_result.frame_length_bytes ==
                     (universal_gnss_protocols::kUnicoreBinaryHeaderSize + 120u +
                      universal_gnss_protocols::kUnicoreBinaryCrcSize),
             "stream detector should classify a valid Unicore binary frame");

  const auto unknown_result = detector.Detect(std::vector<std::uint8_t>{0x10u, 0x20u, 0x30u});
  ctx.Expect(unknown_result.protocol == DetectedStreamProtocol::kUnknown &&
                 unknown_result.bytes_consumed == 0u,
             "stream detector should leave noise-only data as unknown");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestProtocolAndFeatureFlags(ctx);
  TestProfiles(ctx);
  TestStreamDetection(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver foundation tests passed\n";
  return EXIT_SUCCESS;
}
