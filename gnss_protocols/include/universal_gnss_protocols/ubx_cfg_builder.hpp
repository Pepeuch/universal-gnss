#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace universal_gnss_protocols
{

enum class UbxCfgLayer : std::uint8_t
{
  kRam = 0,
  kBbr = 1,
  kFlash = 2,
  kDefault = 7,
};

enum class UbxCfgTransaction : std::uint8_t
{
  kNone = 0,
  kStart = 1,
  kOngoing = 2,
  kApply = 3,
};

enum class UbxCfgValueType : std::uint8_t
{
  kBoolean = 0,
  kU1 = 1,
  kU2 = 2,
  kU4 = 3,
  kI1 = 4,
  kI2 = 5,
  kI4 = 6,
};

enum class UbxCfgBuilderStatus : std::uint8_t
{
  kOk = 0,
  kInvalidArgument = 1,
  kSizeMismatch = 2,
  kTooManyItems = 3,
};

struct UbxCfgValue
{
  UbxCfgValueType type{UbxCfgValueType::kU1};
  std::uint64_t raw_value{0u};

  static UbxCfgValue Boolean(bool value);
  static UbxCfgValue U1(std::uint8_t value);
  static UbxCfgValue U2(std::uint16_t value);
  static UbxCfgValue U4(std::uint32_t value);
  static UbxCfgValue I1(std::int8_t value);
  static UbxCfgValue I2(std::int16_t value);
  static UbxCfgValue I4(std::int32_t value);
};

struct UbxCfgKeyValue
{
  std::uint32_t key_id{0u};
  UbxCfgValue value{};
};

struct UbxCfgBuilderResult
{
  UbxCfgBuilderStatus status{UbxCfgBuilderStatus::kOk};
  std::vector<std::uint8_t> payload{};
  std::vector<std::uint8_t> frame{};
  std::string error_message{};
};

enum class UbxCfgConstellation : std::uint8_t
{
  kGps = 0,
  kGalileo = 1,
  kBeiDou = 2,
  kGlonass = 3,
};

namespace ubx_cfg_keys
{

constexpr std::uint32_t kUart1Baudrate = 0x40520001u;
constexpr std::uint32_t kRateMeas = 0x30210001u;
constexpr std::uint32_t kSignalGpsEnable = 0x1031001Fu;
constexpr std::uint32_t kSignalGalEnable = 0x10310021u;
constexpr std::uint32_t kSignalBdsEnable = 0x10310022u;
constexpr std::uint32_t kSignalGloEnable = 0x10310025u;
constexpr std::uint32_t kMsgoutUbxNavPvtUart1 = 0x20910007u;
constexpr std::uint32_t kMsgoutUbxNavSatUart1 = 0x20910016u;
constexpr std::uint32_t kMsgoutUbxNavStatusUart1 = 0x2091001Bu;
constexpr std::uint32_t kMsgoutUbxMonRfUart1 = 0x2091035Au;
constexpr std::uint32_t kMsgoutNmeaGgaUart1 = 0x209100BBu;

}  // namespace ubx_cfg_keys

UbxCfgBuilderResult BuildUbxCfgValsetFrame(
    std::initializer_list<UbxCfgLayer> layers,
    const std::vector<UbxCfgKeyValue>& key_values,
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildUbxCfgValsetFrame(
    const std::vector<UbxCfgLayer>& layers,
    const std::vector<UbxCfgKeyValue>& key_values,
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildUbxCfgValsetFrame(
    std::initializer_list<UbxCfgLayer> layers,
    const UbxCfgKeyValue& key_value,
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildUbxCfgValsetFrame(
    const std::vector<UbxCfgLayer>& layers,
    const UbxCfgKeyValue& key_value,
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildUbxCfgValgetFrame(UbxCfgLayer layer,
                                           const std::vector<std::uint32_t>& keys,
                                           std::uint16_t position = 0u);

UbxCfgBuilderResult BuildUbxCfgValgetFrame(UbxCfgLayer layer,
                                           std::uint32_t key,
                                           std::uint16_t position = 0u);

UbxCfgBuilderResult BuildEnableMessageRateFrame(
    std::uint32_t message_rate_key,
    std::uint8_t rate,
    std::initializer_list<UbxCfgLayer> layers = {UbxCfgLayer::kRam},
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildEnableMessageRateFrame(
    std::uint32_t message_rate_key,
    std::uint8_t rate,
    const std::vector<UbxCfgLayer>& layers,
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildDisableMessageFrame(
    std::uint32_t message_rate_key,
    std::initializer_list<UbxCfgLayer> layers = {UbxCfgLayer::kRam},
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildDisableMessageFrame(
    std::uint32_t message_rate_key,
    const std::vector<UbxCfgLayer>& layers,
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildUart1BaudrateFrame(
    std::uint32_t baud_rate,
    std::initializer_list<UbxCfgLayer> layers = {UbxCfgLayer::kRam},
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildUart1BaudrateFrame(
    std::uint32_t baud_rate,
    const std::vector<UbxCfgLayer>& layers,
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildRateHzFrame(
    double rate_hz,
    std::initializer_list<UbxCfgLayer> layers = {UbxCfgLayer::kRam},
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildRateHzFrame(
    double rate_hz,
    const std::vector<UbxCfgLayer>& layers,
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildEnableConstellationFrame(
    UbxCfgConstellation constellation,
    bool enabled,
    std::initializer_list<UbxCfgLayer> layers = {UbxCfgLayer::kRam},
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

UbxCfgBuilderResult BuildEnableConstellationFrame(
    UbxCfgConstellation constellation,
    bool enabled,
    const std::vector<UbxCfgLayer>& layers,
    UbxCfgTransaction transaction = UbxCfgTransaction::kNone);

}  // namespace universal_gnss_protocols
