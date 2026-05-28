#include "universal_gnss_protocols/ubx_cfg_builder.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "universal_gnss_protocols/ubx_checksum.hpp"

namespace universal_gnss_protocols
{

namespace
{

constexpr std::uint8_t kUbxClassCfg = 0x06u;
constexpr std::uint8_t kUbxIdCfgValset = 0x8Au;
constexpr std::uint8_t kUbxIdCfgValget = 0x8Bu;
constexpr std::size_t kMaxCfgItemsPerMessage = 64u;

std::size_t EncodedValueSize(const UbxCfgValueType type)
{
  switch (type)
  {
    case UbxCfgValueType::kBoolean:
    case UbxCfgValueType::kU1:
    case UbxCfgValueType::kI1:
      return 1u;
    case UbxCfgValueType::kU2:
    case UbxCfgValueType::kI2:
      return 2u;
    case UbxCfgValueType::kU4:
    case UbxCfgValueType::kI4:
      return 4u;
  }

  return 0u;
}

std::uint8_t KeySizeCode(const std::uint32_t key_id)
{
  return static_cast<std::uint8_t>((key_id >> 28u) & 0x07u);
}

bool DoesValueTypeMatchKey(const std::uint32_t key_id, const UbxCfgValueType type)
{
  const std::uint8_t size_code = KeySizeCode(key_id);
  switch (type)
  {
    case UbxCfgValueType::kBoolean:
      return size_code == 0x01u;
    case UbxCfgValueType::kU1:
    case UbxCfgValueType::kI1:
      return size_code == 0x02u;
    case UbxCfgValueType::kU2:
    case UbxCfgValueType::kI2:
      return size_code == 0x03u;
    case UbxCfgValueType::kU4:
    case UbxCfgValueType::kI4:
      return size_code == 0x04u;
  }

  return false;
}

void AppendLeU2(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
}

void AppendLeU4(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
}

void AppendValue(std::vector<std::uint8_t>& bytes, const UbxCfgValue& value)
{
  switch (value.type)
  {
    case UbxCfgValueType::kBoolean:
    case UbxCfgValueType::kU1:
    case UbxCfgValueType::kI1:
      bytes.push_back(static_cast<std::uint8_t>(value.raw_value & 0xFFu));
      return;
    case UbxCfgValueType::kU2:
    case UbxCfgValueType::kI2:
      AppendLeU2(bytes, static_cast<std::uint16_t>(value.raw_value & 0xFFFFu));
      return;
    case UbxCfgValueType::kU4:
    case UbxCfgValueType::kI4:
      AppendLeU4(bytes, static_cast<std::uint32_t>(value.raw_value & 0xFFFFFFFFu));
      return;
  }
}

std::uint8_t BuildValsetLayerMask(const std::initializer_list<UbxCfgLayer> layers)
{
  std::uint8_t mask = 0u;
  for (const auto layer : layers)
  {
    switch (layer)
    {
      case UbxCfgLayer::kRam:
        mask = static_cast<std::uint8_t>(mask | (1u << 0u));
        break;
      case UbxCfgLayer::kBbr:
        mask = static_cast<std::uint8_t>(mask | (1u << 1u));
        break;
      case UbxCfgLayer::kFlash:
        mask = static_cast<std::uint8_t>(mask | (1u << 2u));
        break;
      case UbxCfgLayer::kDefault:
        break;
    }
  }
  return mask;
}

std::uint8_t BuildValsetLayerMask(const std::vector<UbxCfgLayer>& layers)
{
  std::uint8_t mask = 0u;
  for (const auto layer : layers)
  {
    switch (layer)
    {
      case UbxCfgLayer::kRam:
        mask = static_cast<std::uint8_t>(mask | (1u << 0u));
        break;
      case UbxCfgLayer::kBbr:
        mask = static_cast<std::uint8_t>(mask | (1u << 1u));
        break;
      case UbxCfgLayer::kFlash:
        mask = static_cast<std::uint8_t>(mask | (1u << 2u));
        break;
      case UbxCfgLayer::kDefault:
        break;
    }
  }
  return mask;
}

UbxCfgBuilderResult MakeError(const UbxCfgBuilderStatus status, const char* message)
{
  UbxCfgBuilderResult result;
  result.status = status;
  result.error_message = message;
  return result;
}

std::vector<std::uint8_t> BuildUbxFrame(const std::uint8_t message_class,
                                        const std::uint8_t message_id,
                                        const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> frame;
  frame.reserve(6u + payload.size() + 2u);
  frame.push_back(0xB5u);
  frame.push_back(0x62u);
  frame.push_back(message_class);
  frame.push_back(message_id);
  AppendLeU2(frame, static_cast<std::uint16_t>(payload.size()));
  frame.insert(frame.end(), payload.begin(), payload.end());

  const UbxChecksum checksum = ComputeUbxChecksum(frame.data() + 2u, frame.size() - 2u);
  frame.push_back(checksum.ck_a);
  frame.push_back(checksum.ck_b);
  return frame;
}

UbxCfgBuilderResult MakeFrameResult(const std::vector<std::uint8_t>& payload,
                                    const std::uint8_t message_id)
{
  UbxCfgBuilderResult result;
  result.status = UbxCfgBuilderStatus::kOk;
  result.payload = payload;
  result.frame = BuildUbxFrame(kUbxClassCfg, message_id, payload);
  return result;
}

std::uint32_t ConstellationEnableKey(const UbxCfgConstellation constellation)
{
  switch (constellation)
  {
    case UbxCfgConstellation::kGps:
      return ubx_cfg_keys::kSignalGpsEnable;
    case UbxCfgConstellation::kGalileo:
      return ubx_cfg_keys::kSignalGalEnable;
    case UbxCfgConstellation::kBeiDou:
      return ubx_cfg_keys::kSignalBdsEnable;
    case UbxCfgConstellation::kGlonass:
      return ubx_cfg_keys::kSignalGloEnable;
  }

  return 0u;
}

}  // namespace

UbxCfgValue UbxCfgValue::Boolean(const bool value)
{
  return UbxCfgValue{UbxCfgValueType::kBoolean, value ? 1u : 0u};
}

UbxCfgValue UbxCfgValue::U1(const std::uint8_t value)
{
  return UbxCfgValue{UbxCfgValueType::kU1, value};
}

UbxCfgValue UbxCfgValue::U2(const std::uint16_t value)
{
  return UbxCfgValue{UbxCfgValueType::kU2, value};
}

UbxCfgValue UbxCfgValue::U4(const std::uint32_t value)
{
  return UbxCfgValue{UbxCfgValueType::kU4, value};
}

UbxCfgValue UbxCfgValue::I1(const std::int8_t value)
{
  return UbxCfgValue{UbxCfgValueType::kI1, static_cast<std::uint8_t>(value)};
}

UbxCfgValue UbxCfgValue::I2(const std::int16_t value)
{
  return UbxCfgValue{UbxCfgValueType::kI2, static_cast<std::uint16_t>(value)};
}

UbxCfgValue UbxCfgValue::I4(const std::int32_t value)
{
  return UbxCfgValue{UbxCfgValueType::kI4, static_cast<std::uint32_t>(value)};
}

UbxCfgBuilderResult BuildUbxCfgValsetFrame(
    const std::initializer_list<UbxCfgLayer> layers,
    const std::vector<UbxCfgKeyValue>& key_values,
    const UbxCfgTransaction transaction)
{
  if (key_values.size() > kMaxCfgItemsPerMessage)
  {
    return MakeError(UbxCfgBuilderStatus::kTooManyItems, "too many key/value pairs");
  }

  const std::uint8_t layer_mask = BuildValsetLayerMask(layers);
  if (layer_mask == 0u)
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "at least one VALSET layer is required");
  }

  std::vector<std::uint8_t> payload;
  payload.reserve(4u + key_values.size() * 8u);
  payload.push_back(0x01u);
  payload.push_back(layer_mask);
  payload.push_back(static_cast<std::uint8_t>(transaction));
  payload.push_back(0x00u);

  for (const auto& key_value : key_values)
  {
    if (!DoesValueTypeMatchKey(key_value.key_id, key_value.value.type))
    {
      return MakeError(UbxCfgBuilderStatus::kSizeMismatch, "key/value type size mismatch");
    }

    AppendLeU4(payload, key_value.key_id);
    AppendValue(payload, key_value.value);
  }

  return MakeFrameResult(payload, kUbxIdCfgValset);
}

UbxCfgBuilderResult BuildUbxCfgValsetFrame(
    const std::vector<UbxCfgLayer>& layers,
    const std::vector<UbxCfgKeyValue>& key_values,
    const UbxCfgTransaction transaction)
{
  if (key_values.size() > kMaxCfgItemsPerMessage)
  {
    return MakeError(UbxCfgBuilderStatus::kTooManyItems, "too many key/value pairs");
  }

  const std::uint8_t layer_mask = BuildValsetLayerMask(layers);
  if (layer_mask == 0u)
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "at least one VALSET layer is required");
  }

  std::vector<std::uint8_t> payload;
  payload.reserve(4u + key_values.size() * 8u);
  payload.push_back(0x01u);
  payload.push_back(layer_mask);
  payload.push_back(static_cast<std::uint8_t>(transaction));
  payload.push_back(0x00u);

  for (const auto& key_value : key_values)
  {
    if (!DoesValueTypeMatchKey(key_value.key_id, key_value.value.type))
    {
      return MakeError(UbxCfgBuilderStatus::kSizeMismatch, "key/value type size mismatch");
    }

    AppendLeU4(payload, key_value.key_id);
    AppendValue(payload, key_value.value);
  }

  return MakeFrameResult(payload, kUbxIdCfgValset);
}

UbxCfgBuilderResult BuildUbxCfgValsetFrame(
    const std::initializer_list<UbxCfgLayer> layers,
    const UbxCfgKeyValue& key_value,
    const UbxCfgTransaction transaction)
{
  return BuildUbxCfgValsetFrame(layers, std::vector<UbxCfgKeyValue>{key_value}, transaction);
}

UbxCfgBuilderResult BuildUbxCfgValsetFrame(
    const std::vector<UbxCfgLayer>& layers,
    const UbxCfgKeyValue& key_value,
    const UbxCfgTransaction transaction)
{
  return BuildUbxCfgValsetFrame(layers, std::vector<UbxCfgKeyValue>{key_value}, transaction);
}

UbxCfgBuilderResult BuildUbxCfgValgetFrame(const UbxCfgLayer layer,
                                           const std::vector<std::uint32_t>& keys,
                                           const std::uint16_t position)
{
  if (keys.empty())
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "at least one key is required");
  }
  if (keys.size() > kMaxCfgItemsPerMessage)
  {
    return MakeError(UbxCfgBuilderStatus::kTooManyItems, "too many keys");
  }

  std::vector<std::uint8_t> payload;
  payload.reserve(4u + keys.size() * 4u);
  payload.push_back(0x00u);
  payload.push_back(static_cast<std::uint8_t>(layer));
  AppendLeU2(payload, position);

  for (const auto key : keys)
  {
    AppendLeU4(payload, key);
  }

  return MakeFrameResult(payload, kUbxIdCfgValget);
}

UbxCfgBuilderResult BuildUbxCfgValgetFrame(const UbxCfgLayer layer,
                                           const std::uint32_t key,
                                           const std::uint16_t position)
{
  return BuildUbxCfgValgetFrame(layer, std::vector<std::uint32_t>{key}, position);
}

UbxCfgBuilderResult BuildEnableMessageRateFrame(
    const std::uint32_t message_rate_key,
    const std::uint8_t rate,
    const std::initializer_list<UbxCfgLayer> layers,
    const UbxCfgTransaction transaction)
{
  return BuildUbxCfgValsetFrame(
      layers,
      UbxCfgKeyValue{message_rate_key, UbxCfgValue::U1(rate)},
      transaction);
}

UbxCfgBuilderResult BuildEnableMessageRateFrame(
    const std::uint32_t message_rate_key,
    const std::uint8_t rate,
    const std::vector<UbxCfgLayer>& layers,
    const UbxCfgTransaction transaction)
{
  return BuildUbxCfgValsetFrame(
      layers,
      UbxCfgKeyValue{message_rate_key, UbxCfgValue::U1(rate)},
      transaction);
}

UbxCfgBuilderResult BuildDisableMessageFrame(
    const std::uint32_t message_rate_key,
    const std::initializer_list<UbxCfgLayer> layers,
    const UbxCfgTransaction transaction)
{
  return BuildEnableMessageRateFrame(message_rate_key, 0u, layers, transaction);
}

UbxCfgBuilderResult BuildDisableMessageFrame(
    const std::uint32_t message_rate_key,
    const std::vector<UbxCfgLayer>& layers,
    const UbxCfgTransaction transaction)
{
  return BuildEnableMessageRateFrame(message_rate_key, 0u, layers, transaction);
}

UbxCfgBuilderResult BuildUart1BaudrateFrame(
    const std::uint32_t baud_rate,
    const std::initializer_list<UbxCfgLayer> layers,
    const UbxCfgTransaction transaction)
{
  if (baud_rate == 0u)
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "baud rate must be non-zero");
  }

  return BuildUbxCfgValsetFrame(
      layers,
      UbxCfgKeyValue{ubx_cfg_keys::kUart1Baudrate, UbxCfgValue::U4(baud_rate)},
      transaction);
}

UbxCfgBuilderResult BuildUart1BaudrateFrame(
    const std::uint32_t baud_rate,
    const std::vector<UbxCfgLayer>& layers,
    const UbxCfgTransaction transaction)
{
  if (baud_rate == 0u)
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "baud rate must be non-zero");
  }

  return BuildUbxCfgValsetFrame(
      layers,
      UbxCfgKeyValue{ubx_cfg_keys::kUart1Baudrate, UbxCfgValue::U4(baud_rate)},
      transaction);
}

UbxCfgBuilderResult BuildRateHzFrame(const double rate_hz,
                                     const std::initializer_list<UbxCfgLayer> layers,
                                     const UbxCfgTransaction transaction)
{
  if (!(rate_hz > 0.0))
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "rate must be positive");
  }

  const double period_ms = 1000.0 / rate_hz;
  if (period_ms < 1.0 || period_ms > 65535.0)
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "rate is out of supported range");
  }

  const std::uint16_t meas_period_ms = static_cast<std::uint16_t>(std::llround(period_ms));
  if (meas_period_ms == 0u)
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "rate produced an invalid period");
  }

  return BuildUbxCfgValsetFrame(
      layers,
      UbxCfgKeyValue{ubx_cfg_keys::kRateMeas, UbxCfgValue::U2(meas_period_ms)},
      transaction);
}

UbxCfgBuilderResult BuildRateHzFrame(const double rate_hz,
                                     const std::vector<UbxCfgLayer>& layers,
                                     const UbxCfgTransaction transaction)
{
  if (!(rate_hz > 0.0))
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "rate must be positive");
  }

  const double period_ms = 1000.0 / rate_hz;
  if (period_ms < 1.0 || period_ms > 65535.0)
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "rate is out of supported range");
  }

  const std::uint16_t meas_period_ms = static_cast<std::uint16_t>(std::llround(period_ms));
  if (meas_period_ms == 0u)
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "rate produced an invalid period");
  }

  return BuildUbxCfgValsetFrame(
      layers,
      UbxCfgKeyValue{ubx_cfg_keys::kRateMeas, UbxCfgValue::U2(meas_period_ms)},
      transaction);
}

UbxCfgBuilderResult BuildEnableConstellationFrame(
    const UbxCfgConstellation constellation,
    const bool enabled,
    const std::initializer_list<UbxCfgLayer> layers,
    const UbxCfgTransaction transaction)
{
  const std::uint32_t key = ConstellationEnableKey(constellation);
  if (key == 0u)
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "unsupported constellation");
  }

  return BuildUbxCfgValsetFrame(
      layers,
      UbxCfgKeyValue{key, UbxCfgValue::Boolean(enabled)},
      transaction);
}

UbxCfgBuilderResult BuildEnableConstellationFrame(
    const UbxCfgConstellation constellation,
    const bool enabled,
    const std::vector<UbxCfgLayer>& layers,
    const UbxCfgTransaction transaction)
{
  const std::uint32_t key = ConstellationEnableKey(constellation);
  if (key == 0u)
  {
    return MakeError(UbxCfgBuilderStatus::kInvalidArgument, "unsupported constellation");
  }

  return BuildUbxCfgValsetFrame(
      layers,
      UbxCfgKeyValue{key, UbxCfgValue::Boolean(enabled)},
      transaction);
}

}  // namespace universal_gnss_protocols
