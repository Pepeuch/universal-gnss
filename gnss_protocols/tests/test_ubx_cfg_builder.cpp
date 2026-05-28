#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_protocols/ubx_cfg_builder.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"

namespace
{

using universal_gnss_protocols::UbxCfgBuilderStatus;
using universal_gnss_protocols::UbxCfgKeyValue;
using universal_gnss_protocols::UbxCfgLayer;
using universal_gnss_protocols::UbxCfgTransaction;
using universal_gnss_protocols::UbxCfgValue;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutNmeaGgaUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavPvtUart1;
using universal_gnss_protocols::ubx_cfg_keys::kRateMeas;
using universal_gnss_protocols::ubx_cfg_keys::kSignalGalEnable;
using universal_gnss_protocols::ubx_cfg_keys::kUart1Baudrate;

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

std::uint16_t PayloadLengthFromFrame(const std::vector<std::uint8_t>& frame)
{
  return static_cast<std::uint16_t>(frame[4u]) |
         (static_cast<std::uint16_t>(frame[5u]) << 8u);
}

bool FrameChecksumMatches(const std::vector<std::uint8_t>& frame)
{
  if (frame.size() < 8u)
  {
    return false;
  }

  const auto checksum = universal_gnss_protocols::ComputeUbxChecksum(
      frame.data() + 2u, frame.size() - 4u);
  return checksum.ck_a == frame[frame.size() - 2u] &&
         checksum.ck_b == frame[frame.size() - 1u];
}

void TestValsetFrameGeneration(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::BuildUbxCfgValsetFrame(
      {UbxCfgLayer::kRam, UbxCfgLayer::kBbr},
      UbxCfgKeyValue{kUart1Baudrate, UbxCfgValue::U4(921600u)});

  ctx.Expect(result.status == UbxCfgBuilderStatus::kOk,
             "single-key VALSET should build successfully");
  ctx.Expect(result.payload.size() == 12u &&
                 result.payload[0u] == 0x01u &&
                 result.payload[1u] == 0x03u &&
                 result.payload[2u] == 0x00u &&
                 result.payload[3u] == 0x00u,
             "VALSET payload should use version 1 and pack layer bits correctly");
  ctx.Expect(result.payload[4u] == 0x01u &&
                 result.payload[5u] == 0x00u &&
                 result.payload[6u] == 0x52u &&
                 result.payload[7u] == 0x40u,
             "VALSET should pack keys in little-endian order");
  ctx.Expect(result.payload[8u] == 0x00u &&
                 result.payload[9u] == 0x10u &&
                 result.payload[10u] == 0x0Eu &&
                 result.payload[11u] == 0x00u,
             "VALSET should pack U4 values in little-endian order");
  ctx.Expect(result.frame.size() == 20u &&
                 result.frame[0u] == 0xB5u &&
                 result.frame[1u] == 0x62u &&
                 result.frame[2u] == 0x06u &&
                 result.frame[3u] == 0x8Au &&
                 PayloadLengthFromFrame(result.frame) == result.payload.size() &&
                 FrameChecksumMatches(result.frame),
             "VALSET should produce a complete UBX frame with a valid checksum");
}

void TestValgetFrameGeneration(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::BuildUbxCfgValgetFrame(
      UbxCfgLayer::kRam,
      std::vector<std::uint32_t>{kUart1Baudrate, kMsgoutUbxNavPvtUart1},
      5u);

  ctx.Expect(result.status == UbxCfgBuilderStatus::kOk,
             "VALGET should build successfully");
  ctx.Expect(result.payload.size() == 12u &&
                 result.payload[0u] == 0x00u &&
                 result.payload[1u] == 0x00u &&
                 result.payload[2u] == 0x05u &&
                 result.payload[3u] == 0x00u,
             "VALGET should use request version 0 and pack the position field");
  ctx.Expect(result.payload[4u] == 0x01u &&
                 result.payload[5u] == 0x00u &&
                 result.payload[6u] == 0x52u &&
                 result.payload[7u] == 0x40u &&
                 result.payload[8u] == 0x07u &&
                 result.payload[9u] == 0x00u &&
                 result.payload[10u] == 0x91u &&
                 result.payload[11u] == 0x20u,
             "VALGET should pack multiple keys in little-endian order");
  ctx.Expect(result.frame[2u] == 0x06u &&
                 result.frame[3u] == 0x8Bu &&
                 FrameChecksumMatches(result.frame),
             "VALGET should produce a complete UBX frame with a valid checksum");
}

void TestMultipleKeyValuePacking(TestContext& ctx)
{
  const auto result = universal_gnss_protocols::BuildUbxCfgValsetFrame(
      {UbxCfgLayer::kRam, UbxCfgLayer::kFlash},
      std::vector<UbxCfgKeyValue>{
          {kRateMeas, UbxCfgValue::U2(100u)},
          {kMsgoutUbxNavPvtUart1, UbxCfgValue::U1(1u)},
          {kSignalGalEnable, UbxCfgValue::Boolean(true)},
      },
      UbxCfgTransaction::kApply);

  ctx.Expect(result.status == UbxCfgBuilderStatus::kOk,
             "multi-key VALSET should build successfully");
  ctx.Expect(result.payload[1u] == 0x05u && result.payload[2u] == 0x03u,
             "VALSET should encode multiple layers and transaction actions");
  ctx.Expect(result.payload.size() == 4u + 6u + 5u + 5u,
             "VALSET should size payloads according to each value width");
  ctx.Expect(result.payload[8u] == 0x64u && result.payload[9u] == 0x00u,
             "U2 values should be packed in little-endian order");
  ctx.Expect(result.payload[14u] == 0x01u,
             "U1 values should be packed as one byte");
  ctx.Expect(result.payload.back() == 0x01u,
             "logical values should be packed as a one-byte boolean");
}

void TestHelperBuilders(TestContext& ctx)
{
  const auto enable_nav_pvt =
      universal_gnss_protocols::BuildEnableMessageRateFrame(kMsgoutUbxNavPvtUart1, 1u);
  ctx.Expect(enable_nav_pvt.status == UbxCfgBuilderStatus::kOk &&
                 enable_nav_pvt.payload[4u] == 0x07u &&
                 enable_nav_pvt.payload.back() == 0x01u,
             "enable-message helper should target the requested output key");

  const auto disable_gga =
      universal_gnss_protocols::BuildDisableMessageFrame(kMsgoutNmeaGgaUart1);
  ctx.Expect(disable_gga.status == UbxCfgBuilderStatus::kOk &&
                 disable_gga.payload.back() == 0x00u,
             "disable-message helper should force a zero output rate");

  const auto baud = universal_gnss_protocols::BuildUart1BaudrateFrame(460800u);
  ctx.Expect(baud.status == UbxCfgBuilderStatus::kOk &&
                 baud.payload[8u] == 0x00u &&
                 baud.payload[9u] == 0x08u &&
                 baud.payload[10u] == 0x07u &&
                 baud.payload[11u] == 0x00u,
             "baud-rate helper should pack UART1 baud rate as a U4");

  const auto rate_hz = universal_gnss_protocols::BuildRateHzFrame(10.0);
  ctx.Expect(rate_hz.status == UbxCfgBuilderStatus::kOk &&
                 rate_hz.payload[4u] == 0x01u &&
                 rate_hz.payload[5u] == 0x00u &&
                 rate_hz.payload[6u] == 0x21u &&
                 rate_hz.payload[7u] == 0x30u &&
                 rate_hz.payload[8u] == 100u &&
                 rate_hz.payload[9u] == 0u,
             "rate helper should convert hertz to CFG-RATE-MEAS milliseconds");

  const auto enable_galileo = universal_gnss_protocols::BuildEnableConstellationFrame(
      universal_gnss_protocols::UbxCfgConstellation::kGalileo, true);
  ctx.Expect(enable_galileo.status == UbxCfgBuilderStatus::kOk &&
                 enable_galileo.payload[4u] == 0x21u &&
                 enable_galileo.payload[5u] == 0x00u &&
                 enable_galileo.payload[6u] == 0x31u &&
                 enable_galileo.payload[7u] == 0x10u &&
                 enable_galileo.payload.back() == 0x01u,
             "constellation helper should target the documented enable key");
}

void TestRejectedInputs(TestContext& ctx)
{
  const auto bad_size = universal_gnss_protocols::BuildUbxCfgValsetFrame(
      {UbxCfgLayer::kRam},
      UbxCfgKeyValue{kUart1Baudrate, UbxCfgValue::U1(1u)});
  ctx.Expect(bad_size.status == UbxCfgBuilderStatus::kSizeMismatch,
             "builder should reject value types that do not match the key size");

  const auto no_layers = universal_gnss_protocols::BuildUbxCfgValsetFrame(
      {},
      UbxCfgKeyValue{kMsgoutUbxNavPvtUart1, UbxCfgValue::U1(1u)});
  ctx.Expect(no_layers.status == UbxCfgBuilderStatus::kInvalidArgument,
             "VALSET should require at least one writable layer");

  const auto no_keys =
      universal_gnss_protocols::BuildUbxCfgValgetFrame(UbxCfgLayer::kRam, std::vector<std::uint32_t>{});
  ctx.Expect(no_keys.status == UbxCfgBuilderStatus::kInvalidArgument,
             "VALGET should reject empty key lists");

  const auto bad_rate = universal_gnss_protocols::BuildRateHzFrame(0.0);
  ctx.Expect(bad_rate.status == UbxCfgBuilderStatus::kInvalidArgument,
             "rate helper should reject non-positive rates");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestValsetFrameGeneration(ctx);
  TestValgetFrameGeneration(ctx);
  TestMultipleKeyValuePacking(ctx);
  TestHelperBuilders(ctx);
  TestRejectedInputs(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_protocols UBX CFG builder tests passed\n";
  return EXIT_SUCCESS;
}
