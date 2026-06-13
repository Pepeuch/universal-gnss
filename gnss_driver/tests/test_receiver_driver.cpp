#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/receiver_capabilities.hpp"
#include "universal_gnss_driver/receiver_driver.hpp"
#include "universal_gnss_driver/nmea_driver.hpp"
#include "universal_gnss_driver/ublox_driver.hpp"
#include "universal_gnss_driver/unicore_driver.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"

namespace
{

using universal_gnss::GnssFixType;
using universal_gnss::GnssRtkMode;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
using universal_gnss_driver::ReceiverConfigProfileKind;
using universal_gnss_driver::ReceiverDriver;
using universal_gnss_driver::ReceiverDriverProfileBuildStatus;
using universal_gnss_driver::ReceiverFeature;
using universal_gnss_driver::ReceiverProtocol;
using universal_gnss_driver::ReceiverVendor;
using universal_gnss_driver::NmeaDriver;
using universal_gnss_driver::UbloxDriver;
using universal_gnss_driver::UnicoreDriver;

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

std::string BuildUnicoreAsciiFrame(const std::string& frame_without_crc)
{
  const auto crc = universal_gnss_protocols::ComputeUnicoreBinaryCrc32(
      reinterpret_cast<const std::uint8_t*>(frame_without_crc.data() + 1u),
      frame_without_crc.size() - 1u);

  std::ostringstream stream;
  stream << frame_without_crc
         << '*'
         << std::hex
         << std::nouppercase
         << std::setw(8)
         << std::setfill('0')
         << crc
         << "\r\n";
  return stream.str();
}

void WriteLeU2(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::uint16_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void WriteLeU4(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::uint32_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
  payload[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
  payload[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

void WriteLeI4(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::int32_t value)
{
  WriteLeU4(payload, offset, static_cast<std::uint32_t>(value));
}

std::vector<std::uint8_t> BuildUbxFrame(const std::uint8_t class_id,
                                        const std::uint8_t message_id,
                                        const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> bytes = {
      0xB5u,
      0x62u,
      class_id,
      message_id,
      static_cast<std::uint8_t>(payload.size() & 0xFFu),
      static_cast<std::uint8_t>((payload.size() >> 8u) & 0xFFu),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const auto checksum =
      universal_gnss_protocols::ComputeUbxChecksum(bytes.data() + 2u, bytes.size() - 2u);
  bytes.push_back(checksum.ck_a);
  bytes.push_back(checksum.ck_b);
  return bytes;
}

std::vector<std::uint8_t> BuildNmeaSentence(const std::string& payload)
{
  std::vector<std::uint8_t> bytes;
  bytes.push_back(static_cast<std::uint8_t>('$'));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.push_back(static_cast<std::uint8_t>('*'));

  const std::uint8_t checksum = universal_gnss_protocols::ComputeNmeaChecksum(payload);
  constexpr char kHexDigits[] = "0123456789ABCDEF";
  bytes.push_back(static_cast<std::uint8_t>(kHexDigits[(checksum >> 4u) & 0x0Fu]));
  bytes.push_back(static_cast<std::uint8_t>(kHexDigits[checksum & 0x0Fu]));
  bytes.push_back(static_cast<std::uint8_t>('\r'));
  bytes.push_back(static_cast<std::uint8_t>('\n'));
  return bytes;
}

std::vector<std::uint8_t> MakeNavPvtPayload()
{
  std::vector<std::uint8_t> payload(92u, 0u);
  WriteLeU4(payload, 0u, 345000u);
  WriteLeU2(payload, 4u, 2025u);
  payload[6u] = 5u;
  payload[7u] = 28u;
  payload[8u] = 12u;
  payload[9u] = 34u;
  payload[10u] = 56u;
  payload[11u] = 0x07u;

  payload[20u] = 3u;
  payload[21u] = static_cast<std::uint8_t>(0x01u | (1u << 5));
  payload[23u] = 18u;

  WriteLeI4(payload, 24u, 231234567);
  WriteLeI4(payload, 28u, 485678901);
  WriteLeI4(payload, 32u, 123450);
  WriteLeI4(payload, 36u, 120000);
  WriteLeU4(payload, 40u, 250u);
  WriteLeU4(payload, 44u, 500u);
  WriteLeI4(payload, 84u, 12345678);
  return payload;
}

const std::string kBestNavLine = BuildUnicoreAsciiFrame(
    "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
    "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,1.1053,"
    "2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
    "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123");

void TestDriverFamilyAndCapabilities(TestContext& ctx)
{
  UbloxDriver ublox;
  UnicoreDriver unicore;
  NmeaDriver nmea;

  const ReceiverDriver& ublox_driver = ublox;
  const ReceiverDriver& unicore_driver = unicore;
  const ReceiverDriver& nmea_driver = nmea;

  ctx.Expect(ublox_driver.vendor() == ReceiverVendor::kUblox &&
                 ublox_driver.family() == "F9/F10",
             "u-blox driver should expose the expected vendor and family");
  ctx.Expect(unicore_driver.vendor() == ReceiverVendor::kUnicore &&
                 unicore_driver.family() == "UM98x",
             "Unicore driver should expose the expected vendor and family");
  ctx.Expect(nmea_driver.vendor() == ReceiverVendor::kGeneric &&
                 nmea_driver.family() == "NMEA",
             "generic NMEA driver should expose the expected vendor and family");

  ctx.Expect(universal_gnss_driver::SupportsInputProtocol(
                 ublox_driver.capabilities(), ReceiverProtocol::kUbx) &&
                 universal_gnss_driver::SupportsOutputProtocol(
                     ublox_driver.capabilities(), ReceiverProtocol::kNmea) &&
                 universal_gnss_driver::HasReceiverFeature(
                     ublox_driver.capabilities(), ReceiverFeature::kRtk) &&
                 universal_gnss_driver::HasReceiverFeature(
                     ublox_driver.capabilities(), ReceiverFeature::kRfMonitoring) &&
                 universal_gnss_driver::HasReceiverFeature(
                     ublox_driver.capabilities(), ReceiverFeature::kConstellationConfig) &&
                 universal_gnss_driver::HasReceiverFeature(
                     ublox_driver.capabilities(), ReceiverFeature::kCfgValset),
             "u-blox driver should advertise RTK, RF monitoring, constellation config, and CFG-VALSET support");

  ctx.Expect(universal_gnss_driver::SupportsInputProtocol(
                 unicore_driver.capabilities(), ReceiverProtocol::kUnicoreAscii) &&
                 universal_gnss_driver::SupportsOutputProtocol(
                     unicore_driver.capabilities(), ReceiverProtocol::kUnicoreBinary) &&
                 universal_gnss_driver::HasReceiverFeature(
                     unicore_driver.capabilities(), ReceiverFeature::kRtk) &&
                 universal_gnss_driver::HasReceiverFeature(
                     unicore_driver.capabilities(), ReceiverFeature::kSignalGroups) &&
                 universal_gnss_driver::HasReceiverFeature(
                     unicore_driver.capabilities(), ReceiverFeature::kAsciiCommandConfig),
             "Unicore driver should advertise RTK, signal-group config, and ASCII command config support");

  ctx.Expect(!universal_gnss_driver::SupportsInputProtocol(
                 nmea_driver.capabilities(), ReceiverProtocol::kNmea) &&
                 universal_gnss_driver::SupportsOutputProtocol(
                     nmea_driver.capabilities(), ReceiverProtocol::kNmea) &&
                 universal_gnss_driver::HasReceiverFeature(
                     nmea_driver.capabilities(), ReceiverFeature::kRoverMode) &&
                 !universal_gnss_driver::HasReceiverFeature(
                     nmea_driver.capabilities(), ReceiverFeature::kAsciiCommandConfig),
             "generic NMEA driver should advertise a read-only NMEA output path without config features");
}

void TestSupportedProfilesAndGeneration(TestContext& ctx)
{
  UbloxDriver ublox;
  UnicoreDriver unicore;
  NmeaDriver nmea;

  const ReceiverDriver& ublox_driver = ublox;
  const ReceiverDriver& unicore_driver = unicore;
  const ReceiverDriver& nmea_driver = nmea;

  ctx.Expect(ublox_driver.SupportsProfile(ReceiverConfigProfileKind::kRover) &&
                 ublox_driver.SupportsProfile(ReceiverConfigProfileKind::kDiagnosticsOutput) &&
                 ublox_driver.SupportsProfile(ReceiverConfigProfileKind::kBase),
             "u-blox driver should report rover, diagnostics, and base profile support");
  ctx.Expect(unicore_driver.SupportsProfile(ReceiverConfigProfileKind::kRover) &&
                 unicore_driver.SupportsProfile(ReceiverConfigProfileKind::kDiagnosticsOutput) &&
                 !unicore_driver.SupportsProfile(ReceiverConfigProfileKind::kBase),
             "Unicore driver should report rover/diagnostics support without base support");
  ctx.Expect(!nmea_driver.SupportsProfile(ReceiverConfigProfileKind::kRover) &&
                 !nmea_driver.SupportsProfile(ReceiverConfigProfileKind::kDiagnosticsOutput),
             "generic NMEA driver should expose no configuration profiles");

  const auto ublox_rover = ublox_driver.BuildRoverProfile();
  const auto ublox_diag =
      ublox_driver.BuildDiagnosticsProfile(ReceiverCommandSafetyLevel::kPersistent);
  const auto ublox_base = ublox_driver.BuildBaseProfile();
  ctx.Expect(ublox_rover.status == ReceiverDriverProfileBuildStatus::kOk &&
                 ublox_rover.profile_kind == ReceiverConfigProfileKind::kRover &&
                 ublox_rover.commands.size() == 13u,
             "u-blox rover driver profile should delegate to the existing rover builder");
  ctx.Expect(ublox_diag.status == ReceiverDriverProfileBuildStatus::kOk &&
                 ublox_diag.profile_kind == ReceiverConfigProfileKind::kDiagnosticsOutput &&
                 ublox_diag.commands.size() == 23u &&
                 !ublox_diag.commands.empty() &&
                 ublox_diag.commands.front().safety_level ==
                     ReceiverCommandSafetyLevel::kPersistent,
             "persistent u-blox diagnostics driver profiles should preserve persistent command safety");
  ctx.Expect(ublox_base.status == ReceiverDriverProfileBuildStatus::kOk &&
                 ublox_base.profile_kind == ReceiverConfigProfileKind::kBase &&
                 ublox_base.commands.size() == 11u,
             "u-blox base driver profile should expose the existing base builder");

  const auto unicore_rover = unicore_driver.BuildRoverProfile();
  const auto unicore_diag = unicore_driver.BuildDiagnosticsProfile();
  const auto unicore_base = unicore_driver.BuildBaseProfile();
  const auto nmea_rover = nmea_driver.BuildRoverProfile();
  const auto nmea_diag = nmea_driver.BuildDiagnosticsProfile();
  ctx.Expect(unicore_rover.status == ReceiverDriverProfileBuildStatus::kOk &&
                 unicore_rover.profile_kind == ReceiverConfigProfileKind::kRover &&
                 unicore_rover.commands.size() == 14u,
             "Unicore rover driver profile should delegate to the existing rover builder");
  ctx.Expect(unicore_diag.status == ReceiverDriverProfileBuildStatus::kOk &&
                 unicore_diag.profile_kind == ReceiverConfigProfileKind::kDiagnosticsOutput &&
                 unicore_diag.commands.size() == 15u,
             "Unicore diagnostics driver profile should delegate to the existing diagnostics builder");
  ctx.Expect(unicore_base.status == ReceiverDriverProfileBuildStatus::kUnsupportedProfile &&
                 unicore_base.profile_kind == ReceiverConfigProfileKind::kBase,
             "Unicore drivers should report base profile generation as unsupported");
  ctx.Expect(nmea_rover.status == ReceiverDriverProfileBuildStatus::kUnsupportedProfile &&
                 nmea_rover.profile_kind == ReceiverConfigProfileKind::kRover &&
                 nmea_diag.status == ReceiverDriverProfileBuildStatus::kUnsupportedProfile &&
                 nmea_diag.profile_kind == ReceiverConfigProfileKind::kDiagnosticsOutput,
             "generic NMEA drivers should reject configuration profile generation cleanly");
}

void TestRuntimeStateAccess(TestContext& ctx)
{
  UbloxDriver ublox;
  UnicoreDriver unicore;
  NmeaDriver nmea;

  ReceiverDriver& ublox_driver = ublox;
  ReceiverDriver& unicore_driver = unicore;
  ReceiverDriver& nmea_driver = nmea;

  ctx.Expect(!ublox_driver.current_state().fix_valid &&
                 ublox_driver.current_state().fix_type == GnssFixType::kUnknown,
             "u-blox driver should expose the default empty runtime state before input");
  ctx.Expect(!unicore_driver.current_state().fix_valid &&
                 unicore_driver.current_state().fix_type == GnssFixType::kUnknown,
             "Unicore driver should expose the default empty runtime state before input");
  ctx.Expect(!nmea_driver.current_state().fix_valid &&
                 nmea_driver.current_state().fix_type == GnssFixType::kUnknown,
             "generic NMEA driver should expose the default empty runtime state before input");

  ublox_driver.FeedBytes(BuildUbxFrame(0x01u, 0x07u, MakeNavPvtPayload()), 1000);
  unicore_driver.FeedString(kBestNavLine, 2000);
  nmea_driver.FeedBytes(
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),
      3000);
  nmea_driver.FeedBytes(
      BuildNmeaSentence("GPGST,024603.00,1.2,0.8,0.7,45.0,0.4,0.5,1.1"), 3001);

  ctx.Expect(ublox_driver.current_state().fix_valid &&
                 ublox_driver.current_state().fix_type == GnssFixType::kFix &&
                 ublox_driver.current_state().timestamp_ns == std::optional<std::int64_t>(1000),
             "u-blox driver should surface runtime state from the underlying session");
  ctx.Expect(unicore_driver.current_state().fix_valid &&
                 unicore_driver.current_state().fix_type == GnssFixType::kRtkFloat &&
                 unicore_driver.current_state().rtk_mode ==
                     std::optional<universal_gnss::GnssRtkMode>(GnssRtkMode::kFloat) &&
                 unicore_driver.current_state().timestamp_ns == std::optional<std::int64_t>(2000),
             "Unicore driver should surface runtime state from the underlying session");
  ctx.Expect(nmea_driver.current_state().fix_valid &&
                 nmea_driver.current_state().fix_type == GnssFixType::kFix &&
                 nmea_driver.current_state().timestamp_ns == std::optional<std::int64_t>(3001) &&
                 nmea_driver.current_state().horizontal_accuracy_m == std::optional<float>(0.5f),
             "generic NMEA driver should surface runtime state from the underlying session");

  ublox_driver.Reset();
  unicore_driver.Reset();
  nmea_driver.Reset();

  ctx.Expect(!ublox_driver.current_state().fix_valid &&
                 ublox_driver.current_state().fix_type == GnssFixType::kUnknown &&
                 !unicore_driver.current_state().fix_valid &&
                 unicore_driver.current_state().fix_type == GnssFixType::kUnknown &&
                 !nmea_driver.current_state().fix_valid &&
                 nmea_driver.current_state().fix_type == GnssFixType::kUnknown,
             "resetting drivers should clear the runtime state back to defaults");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestDriverFamilyAndCapabilities(ctx);
  TestSupportedProfilesAndGeneration(ctx);
  TestRuntimeStateAccess(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_driver receiver driver tests passed\n";
  return EXIT_SUCCESS;
}
