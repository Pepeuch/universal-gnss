#include "universal_gnss_driver/ublox_config_profile_builder.hpp"

#include <utility>

namespace universal_gnss_driver
{

namespace
{

using universal_gnss_protocols::BuildDisableMessageFrame;
using universal_gnss_protocols::BuildEnableConstellationFrame;
using universal_gnss_protocols::BuildEnableMessageRateFrame;
using universal_gnss_protocols::BuildRateHzFrame;
using universal_gnss_protocols::BuildUart1BaudrateFrame;
using universal_gnss_protocols::BuildUart2BaudrateFrame;
using universal_gnss_protocols::UbxCfgBuilderResult;
using universal_gnss_protocols::UbxCfgBuilderStatus;
using universal_gnss_protocols::UbxCfgConstellation;
using universal_gnss_protocols::UbxCfgLayer;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutNmeaGgaUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutNmeaGgaUart2;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutNmeaGgaUsb;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxMonHw2Uart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxMonHw2Uart2;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxMonHw2Usb;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxMonHwUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxMonHwUart2;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxMonHwUsb;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxMonRfUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxMonRfUart2;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxMonRfUsb;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavDopUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavDopUart2;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavDopUsb;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavPvtUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavPvtUart2;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavPvtUsb;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavSatUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavSatUart2;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavSatUsb;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavStatusUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavStatusUart2;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxNavStatusUsb;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxRxmRtcmUart1;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxRxmRtcmUart2;
using universal_gnss_protocols::ubx_cfg_keys::kMsgoutUbxRxmRtcmUsb;

constexpr ReceiverTargetSelector kUbloxTarget{
    ReceiverVendor::kUblox,
    "F9/F10",
    "family",
    "ublox_f9_f10",
};

enum class UbloxOutputMessageKind : std::uint8_t
{
  kNavPvt = 0,
  kNavSat = 1,
  kNavStatus = 2,
  kNavDop = 3,
  kMonHw = 4,
  kMonHw2 = 5,
  kMonRf = 6,
  kRxmRtcm = 7,
  kNmeaGga = 8,
};

bool ContainsPersistentLayer(const std::vector<UbxCfgLayer>& layers)
{
  for (const auto layer : layers)
  {
    if (layer == UbxCfgLayer::kBbr || layer == UbxCfgLayer::kFlash)
    {
      return true;
    }
  }

  return false;
}

ReceiverCommandSafetyLevel ResolveSafetyLevel(const UbloxConfigProfile& profile)
{
  if (profile.safety_level == ReceiverCommandSafetyLevel::kFactoryReset)
  {
    return ReceiverCommandSafetyLevel::kFactoryReset;
  }

  if (profile.safety_level == ReceiverCommandSafetyLevel::kPersistent ||
      ContainsPersistentLayer(profile.target_layers))
  {
    return ReceiverCommandSafetyLevel::kPersistent;
  }

  return ReceiverCommandSafetyLevel::kRuntime;
}

ReceiverCommandKind ResolveCommandKind(const bool protocol_output_change)
{
  return protocol_output_change ? ReceiverCommandKind::kSetProtocolOutputs
                                : ReceiverCommandKind::kApplyConfigProfile;
}

ReceiverCommand MakeUbloxCommand(const ReceiverCommandKind kind,
                                 const ReceiverCommandSafetyLevel safety_level,
                                 std::vector<std::uint8_t> frame)
{
  ReceiverCommand command;
  command.kind = kind;
  command.target = kUbloxTarget;
  command.expected_response = ReceiverResponseKind::kAck;
  command.safety_level = safety_level;
  SetBinaryPayload(command, std::move(frame));
  return command;
}

bool AppendBuilderFrame(UbloxConfigProfileBuildResult& result,
                        const ReceiverCommandKind kind,
                        const ReceiverCommandSafetyLevel safety_level,
                        const UbxCfgBuilderResult& builder_result)
{
  if (builder_result.status != UbxCfgBuilderStatus::kOk)
  {
    result.status = UbloxConfigProfileBuildStatus::kBuilderError;
    result.error_message = builder_result.error_message;
    return false;
  }

  result.commands.push_back(
      MakeUbloxCommand(kind, safety_level, builder_result.frame));
  return true;
}

bool ValidateProfile(UbloxConfigProfileBuildResult& result, const UbloxConfigProfile& profile)
{
  if (profile.target_layers.empty())
  {
    result.status = UbloxConfigProfileBuildStatus::kInvalidArgument;
    result.error_message = "u-blox config profile must target at least one CFG layer";
    return false;
  }

  if (profile.safety_level == ReceiverCommandSafetyLevel::kFactoryReset)
  {
    result.status = UbloxConfigProfileBuildStatus::kInvalidArgument;
    result.error_message =
        "u-blox config profile builder does not support factory-reset command generation";
    return false;
  }

  if (profile.measurement_rate_hz.has_value() && *profile.measurement_rate_hz <= 0.0)
  {
    result.status = UbloxConfigProfileBuildStatus::kInvalidArgument;
    result.error_message = "measurement rate must be positive";
    return false;
  }

  for (const auto& message_rate : profile.enabled_messages)
  {
    if (message_rate.message_rate_key == 0u)
    {
      result.status = UbloxConfigProfileBuildStatus::kInvalidArgument;
      result.error_message = "enabled u-blox message rate key must be non-zero";
      return false;
    }
  }

  for (const auto message_rate_key : profile.disabled_messages)
  {
    if (message_rate_key == 0u)
    {
      result.status = UbloxConfigProfileBuildStatus::kInvalidArgument;
      result.error_message = "disabled u-blox message rate key must be non-zero";
      return false;
    }
  }

  if (profile.port.uart1_baudrate.has_value() && *profile.port.uart1_baudrate == 0u)
  {
    result.status = UbloxConfigProfileBuildStatus::kInvalidArgument;
    result.error_message = "u-blox UART1 baud rate must be non-zero";
    return false;
  }

  if (profile.port.uart2_baudrate.has_value() && *profile.port.uart2_baudrate == 0u)
  {
    result.status = UbloxConfigProfileBuildStatus::kInvalidArgument;
    result.error_message = "u-blox UART2 baud rate must be non-zero";
    return false;
  }

  return true;
}

void AppendStandardConstellations(UbloxConfigProfile& profile)
{
  profile.constellations = {
      {UbxCfgConstellation::kGps, true},
      {UbxCfgConstellation::kGalileo, true},
      {UbxCfgConstellation::kBeiDou, true},
      {UbxCfgConstellation::kGlonass, true},
  };
}

std::uint32_t ResolveMessageRateKey(const UbloxOutputMessageKind message,
                                    const UbloxInterfacePort port)
{
  switch (message)
  {
    case UbloxOutputMessageKind::kNavPvt:
      switch (port)
      {
        case UbloxInterfacePort::kUart1:
          return kMsgoutUbxNavPvtUart1;
        case UbloxInterfacePort::kUart2:
          return kMsgoutUbxNavPvtUart2;
        case UbloxInterfacePort::kUsb:
          return kMsgoutUbxNavPvtUsb;
      }
      break;
    case UbloxOutputMessageKind::kNavSat:
      switch (port)
      {
        case UbloxInterfacePort::kUart1:
          return kMsgoutUbxNavSatUart1;
        case UbloxInterfacePort::kUart2:
          return kMsgoutUbxNavSatUart2;
        case UbloxInterfacePort::kUsb:
          return kMsgoutUbxNavSatUsb;
      }
      break;
    case UbloxOutputMessageKind::kNavStatus:
      switch (port)
      {
        case UbloxInterfacePort::kUart1:
          return kMsgoutUbxNavStatusUart1;
        case UbloxInterfacePort::kUart2:
          return kMsgoutUbxNavStatusUart2;
        case UbloxInterfacePort::kUsb:
          return kMsgoutUbxNavStatusUsb;
      }
      break;
    case UbloxOutputMessageKind::kNavDop:
      switch (port)
      {
        case UbloxInterfacePort::kUart1:
          return kMsgoutUbxNavDopUart1;
        case UbloxInterfacePort::kUart2:
          return kMsgoutUbxNavDopUart2;
        case UbloxInterfacePort::kUsb:
          return kMsgoutUbxNavDopUsb;
      }
      break;
    case UbloxOutputMessageKind::kMonHw:
      switch (port)
      {
        case UbloxInterfacePort::kUart1:
          return kMsgoutUbxMonHwUart1;
        case UbloxInterfacePort::kUart2:
          return kMsgoutUbxMonHwUart2;
        case UbloxInterfacePort::kUsb:
          return kMsgoutUbxMonHwUsb;
      }
      break;
    case UbloxOutputMessageKind::kMonHw2:
      switch (port)
      {
        case UbloxInterfacePort::kUart1:
          return kMsgoutUbxMonHw2Uart1;
        case UbloxInterfacePort::kUart2:
          return kMsgoutUbxMonHw2Uart2;
        case UbloxInterfacePort::kUsb:
          return kMsgoutUbxMonHw2Usb;
      }
      break;
    case UbloxOutputMessageKind::kMonRf:
      switch (port)
      {
        case UbloxInterfacePort::kUart1:
          return kMsgoutUbxMonRfUart1;
        case UbloxInterfacePort::kUart2:
          return kMsgoutUbxMonRfUart2;
        case UbloxInterfacePort::kUsb:
          return kMsgoutUbxMonRfUsb;
      }
      break;
    case UbloxOutputMessageKind::kRxmRtcm:
      switch (port)
      {
        case UbloxInterfacePort::kUart1:
          return kMsgoutUbxRxmRtcmUart1;
        case UbloxInterfacePort::kUart2:
          return kMsgoutUbxRxmRtcmUart2;
        case UbloxInterfacePort::kUsb:
          return kMsgoutUbxRxmRtcmUsb;
      }
      break;
    case UbloxOutputMessageKind::kNmeaGga:
      switch (port)
      {
        case UbloxInterfacePort::kUart1:
          return kMsgoutNmeaGgaUart1;
        case UbloxInterfacePort::kUart2:
          return kMsgoutNmeaGgaUart2;
        case UbloxInterfacePort::kUsb:
          return kMsgoutNmeaGgaUsb;
      }
      break;
  }

  return 0u;
}

std::vector<UbloxMessageRate> MakeMessageRates(
    const std::vector<UbloxOutputMessageKind>& messages,
    const std::vector<UbloxInterfacePort>& output_ports)
{
  std::vector<UbloxMessageRate> rates;
  rates.reserve(messages.size() * output_ports.size());

  for (const auto message : messages)
  {
    for (const auto port : output_ports)
    {
      const auto key = ResolveMessageRateKey(message, port);
      if (key != 0u)
      {
        rates.push_back(UbloxMessageRate{key, 1u});
      }
    }
  }

  return rates;
}

}  // namespace

UbloxConfigProfileBuildResult UbloxConfigProfileBuilder::Build(
    const UbloxConfigProfile& profile)
{
  UbloxConfigProfileBuildResult result;
  if (!ValidateProfile(result, profile))
  {
    return result;
  }

  const auto safety_level = ResolveSafetyLevel(profile);

  if (profile.port.uart1_baudrate.has_value())
  {
    if (!AppendBuilderFrame(result,
                            ResolveCommandKind(false),
                            safety_level,
                            BuildUart1BaudrateFrame(*profile.port.uart1_baudrate,
                                                    profile.target_layers)))
    {
      return result;
    }
  }

  if (profile.port.uart2_baudrate.has_value())
  {
    if (!AppendBuilderFrame(result,
                            ResolveCommandKind(false),
                            safety_level,
                            BuildUart2BaudrateFrame(*profile.port.uart2_baudrate,
                                                    profile.target_layers)))
    {
      return result;
    }
  }

  if (profile.measurement_rate_hz.has_value())
  {
    if (!AppendBuilderFrame(result,
                            ResolveCommandKind(false),
                            safety_level,
                            BuildRateHzFrame(*profile.measurement_rate_hz,
                                             profile.target_layers)))
    {
      return result;
    }
  }

  for (const auto& message_rate : profile.enabled_messages)
  {
    if (!AppendBuilderFrame(result,
                            ResolveCommandKind(true),
                            safety_level,
                            BuildEnableMessageRateFrame(message_rate.message_rate_key,
                                                        message_rate.rate,
                                                        profile.target_layers)))
    {
      return result;
    }
  }

  for (const auto message_rate_key : profile.disabled_messages)
  {
    if (!AppendBuilderFrame(result,
                            ResolveCommandKind(true),
                            safety_level,
                            BuildDisableMessageFrame(message_rate_key,
                                                     profile.target_layers)))
    {
      return result;
    }
  }

  for (const auto& constellation : profile.constellations)
  {
    if (!AppendBuilderFrame(result,
                            ResolveCommandKind(false),
                            safety_level,
                            BuildEnableConstellationFrame(constellation.constellation,
                                                          constellation.enabled,
                                                          profile.target_layers)))
    {
      return result;
    }
  }

  return result;
}

UbloxConfigProfile UbloxConfigProfileBuilder::BuildUbloxRoverProfile(
    const ReceiverCommandSafetyLevel safety_level,
    std::vector<UbxCfgLayer> layers,
    std::vector<UbloxInterfacePort> output_ports)
{
  UbloxConfigProfile profile;
  profile.config_kind = ReceiverConfigProfileKind::kRover;
  profile.safety_level = safety_level;
  profile.target_layers = std::move(layers);
  profile.port.output_ports = std::move(output_ports);
  profile.measurement_rate_hz = 10.0;
  profile.enabled_messages = MakeMessageRates(
      {
          UbloxOutputMessageKind::kNavPvt,
          UbloxOutputMessageKind::kNavSat,
          UbloxOutputMessageKind::kNavStatus,
          UbloxOutputMessageKind::kMonRf,
      },
      profile.port.output_ports);
  AppendStandardConstellations(profile);
  return profile;
}

UbloxConfigProfile UbloxConfigProfileBuilder::BuildUbloxBaseProfile(
    const ReceiverCommandSafetyLevel safety_level,
    std::vector<UbxCfgLayer> layers,
    std::vector<UbloxInterfacePort> output_ports)
{
  UbloxConfigProfile profile;
  profile.config_kind = ReceiverConfigProfileKind::kBase;
  profile.safety_level = safety_level;
  profile.target_layers = std::move(layers);
  profile.port.output_ports = std::move(output_ports);
  profile.measurement_rate_hz = 1.0;
  profile.enabled_messages = MakeMessageRates(
      {
          UbloxOutputMessageKind::kNavPvt,
          UbloxOutputMessageKind::kNavStatus,
          UbloxOutputMessageKind::kNmeaGga,
      },
      profile.port.output_ports);
  AppendStandardConstellations(profile);
  return profile;
}

UbloxConfigProfile UbloxConfigProfileBuilder::BuildUbloxDiagnosticsProfile(
    const ReceiverCommandSafetyLevel safety_level,
    std::vector<UbxCfgLayer> layers,
    std::vector<UbloxInterfacePort> output_ports)
{
  UbloxConfigProfile profile;
  profile.config_kind = ReceiverConfigProfileKind::kDiagnosticsOutput;
  profile.safety_level = safety_level;
  profile.target_layers = std::move(layers);
  profile.port.output_ports = std::move(output_ports);
  profile.measurement_rate_hz = 5.0;
  profile.enabled_messages = MakeMessageRates(
      {
          UbloxOutputMessageKind::kNavPvt,
          UbloxOutputMessageKind::kNavSat,
          UbloxOutputMessageKind::kNavStatus,
          UbloxOutputMessageKind::kNavDop,
          UbloxOutputMessageKind::kMonHw,
          UbloxOutputMessageKind::kMonHw2,
          UbloxOutputMessageKind::kMonRf,
          UbloxOutputMessageKind::kRxmRtcm,
          UbloxOutputMessageKind::kNmeaGga,
      },
      profile.port.output_ports);
  AppendStandardConstellations(profile);
  return profile;
}

}  // namespace universal_gnss_driver
