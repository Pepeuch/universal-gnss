#include "universal_gnss_tools/profile_preview.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "universal_gnss_driver/receiver_auto_config.hpp"
#include "universal_gnss_driver/unicore_model_profile.hpp"
#include "universal_gnss_protocols/ubx_cfg_builder.hpp"

namespace universal_gnss_tools
{

namespace
{

using universal_gnss_driver::ReceiverAutoConfigApplyMode;
using universal_gnss_driver::ReceiverAutoConfigPlan;
using universal_gnss_driver::ReceiverAutoConfigPlanStatus;
using universal_gnss_driver::ReceiverCommand;
using universal_gnss_driver::ReceiverCommandKind;
using universal_gnss_driver::ReceiverCommandPayloadKind;
using universal_gnss_driver::ReceiverCommandSafetyLevel;
using universal_gnss_driver::ReceiverDetectedFamily;
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
using universal_gnss_protocols::ubx_cfg_keys::kRateMeas;
using universal_gnss_protocols::ubx_cfg_keys::kSignalBdsEnable;
using universal_gnss_protocols::ubx_cfg_keys::kSignalGalEnable;
using universal_gnss_protocols::ubx_cfg_keys::kSignalGloEnable;
using universal_gnss_protocols::ubx_cfg_keys::kSignalGpsEnable;
using universal_gnss_protocols::ubx_cfg_keys::kUart1Baudrate;
using universal_gnss_protocols::ubx_cfg_keys::kUart2Baudrate;

constexpr std::uint8_t kUbxCfgClass = 0x06u;
constexpr std::uint8_t kUbxCfgValsetId = 0x8Au;

std::string ToLowerCopy(std::string value)
{
  for (char& c : value)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool StartsWith(const std::string_view text, const std::string_view prefix)
{
  return text.size() >= prefix.size() && text.compare(0u, prefix.size(), prefix) == 0;
}

bool LooksLikeUnicorePortToken(const std::string_view token)
{
  return token.size() == 4u && token.substr(0u, 3u) == "COM" &&
         std::isdigit(static_cast<unsigned char>(token[3])) != 0;
}

std::string TrimTrailingCrLf(std::string text)
{
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
  {
    text.pop_back();
  }
  return text;
}

std::string FormatCompactDouble(const double value, const int precision = 3)
{
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(precision) << value;
  std::string text = stream.str();
  while (!text.empty() && text.back() == '0')
  {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.')
  {
    text.pop_back();
  }
  return text.empty() ? "0" : text;
}

std::string EscapeJson(std::string_view text)
{
  std::ostringstream stream;
  for (const unsigned char c : text)
  {
    switch (c)
    {
      case '\\':
        stream << "\\\\";
        break;
      case '"':
        stream << "\\\"";
        break;
      case '\b':
        stream << "\\b";
        break;
      case '\f':
        stream << "\\f";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        if (c < 0x20u)
        {
          stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
                 << std::dec << std::setfill(' ');
        }
        else
        {
          stream << static_cast<char>(c);
        }
        break;
    }
  }
  return stream.str();
}

const char* CommandKindToString(const ReceiverCommandKind kind)
{
  switch (kind)
  {
    case ReceiverCommandKind::kApplyConfigProfile:
      return "ApplyConfigProfile";
    case ReceiverCommandKind::kSetProtocolOutputs:
      return "SetProtocolOutputs";
    case ReceiverCommandKind::kQuery:
      return "Query";
    case ReceiverCommandKind::kRawBinary:
      return "RawBinary";
    case ReceiverCommandKind::kRawText:
      return "RawText";
    case ReceiverCommandKind::kReset:
      return "Reset";
    case ReceiverCommandKind::kUnknown:
      break;
  }

  return "Unknown";
}

const char* SafetyLevelToString(const ReceiverCommandSafetyLevel safety)
{
  switch (safety)
  {
    case ReceiverCommandSafetyLevel::kRuntime:
      return "runtime";
    case ReceiverCommandSafetyLevel::kPersistent:
      return "persistent";
    case ReceiverCommandSafetyLevel::kFactoryReset:
      return "factory_reset";
  }

  return "unknown";
}

const char* PayloadKindToString(const ReceiverCommandPayloadKind payload_kind)
{
  switch (payload_kind)
  {
    case ReceiverCommandPayloadKind::kBinary:
      return "binary";
    case ReceiverCommandPayloadKind::kText:
      return "text";
    case ReceiverCommandPayloadKind::kNone:
      break;
  }

  return "none";
}

std::size_t CommandPayloadSize(const ReceiverCommand& command)
{
  switch (command.payload.kind)
  {
    case ReceiverCommandPayloadKind::kBinary:
      return command.payload.binary.size();
    case ReceiverCommandPayloadKind::kText:
      return command.payload.text.size();
    case ReceiverCommandPayloadKind::kNone:
      break;
  }

  return 0u;
}

std::optional<std::uint32_t> ParsePlannedUnicoreConfigBaud(const ReceiverCommand& command)
{
  if (command.payload.kind != ReceiverCommandPayloadKind::kText)
  {
    return std::nullopt;
  }

  const std::string text = TrimTrailingCrLf(command.payload.text);
  constexpr std::string_view kPrefix = "CONFIG COM1 ";
  if (!StartsWith(text, kPrefix))
  {
    return std::nullopt;
  }

  const std::string_view remainder(text.data() + kPrefix.size(), text.size() - kPrefix.size());
  const std::size_t separator = remainder.find(' ');
  if (separator == std::string_view::npos || separator == 0u)
  {
    return std::nullopt;
  }

  try
  {
    std::size_t parsed = 0u;
    const auto baud = std::stoul(std::string(remainder.substr(0u, separator)), &parsed, 10);
    if (parsed != separator)
    {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(baud);
  }
  catch (...)
  {
    return std::nullopt;
  }
}

std::optional<std::uint32_t> ExtractPlannedUnicoreConfigBaud(
    const std::vector<ProfilePreviewCommand>& commands)
{
  for (const auto& command : commands)
  {
    if (const auto baud = ParsePlannedUnicoreConfigBaud(command.command); baud.has_value())
    {
      return baud;
    }
  }

  return std::nullopt;
}

bool HasFactoryResetCommand(const std::vector<ProfilePreviewCommand>& commands)
{
  for (const auto& command : commands)
  {
    if (command.command.kind == ReceiverCommandKind::kReset)
    {
      return true;
    }
  }

  return false;
}

std::string FormatHexBytes(const std::vector<std::uint8_t>& bytes)
{
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < bytes.size(); ++index)
  {
    if (index != 0u)
    {
      stream << ' ';
    }
    stream << std::setw(2) << static_cast<unsigned int>(bytes[index]);
  }
  return stream.str();
}

std::uint16_t ReadLeU2(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1u] << 8u);
}

std::uint32_t ReadLeU4(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
         (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
         (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

std::string DescribeMessageRateKey(const std::uint32_t key)
{
  switch (key)
  {
    case kMsgoutUbxNavPvtUart1:
    case kMsgoutUbxNavPvtUart2:
    case kMsgoutUbxNavPvtUsb:
      return "NAV-PVT";
    case kMsgoutUbxNavSatUart1:
    case kMsgoutUbxNavSatUart2:
    case kMsgoutUbxNavSatUsb:
      return "NAV-SAT";
    case kMsgoutUbxNavStatusUart1:
    case kMsgoutUbxNavStatusUart2:
    case kMsgoutUbxNavStatusUsb:
      return "NAV-STATUS";
    case kMsgoutUbxNavDopUart1:
    case kMsgoutUbxNavDopUart2:
    case kMsgoutUbxNavDopUsb:
      return "NAV-DOP";
    case kMsgoutUbxMonHwUart1:
    case kMsgoutUbxMonHwUart2:
    case kMsgoutUbxMonHwUsb:
      return "MON-HW";
    case kMsgoutUbxMonHw2Uart1:
    case kMsgoutUbxMonHw2Uart2:
    case kMsgoutUbxMonHw2Usb:
      return "MON-HW2";
    case kMsgoutUbxMonRfUart1:
    case kMsgoutUbxMonRfUart2:
    case kMsgoutUbxMonRfUsb:
      return "MON-RF";
    case kMsgoutNmeaGgaUart1:
    case kMsgoutNmeaGgaUart2:
    case kMsgoutNmeaGgaUsb:
      return "NMEA-GGA";
    case kMsgoutUbxRxmRtcmUart1:
    case kMsgoutUbxRxmRtcmUart2:
    case kMsgoutUbxRxmRtcmUsb:
      return "RXM-RTCM";
  }

  std::ostringstream stream;
  stream << "CFG key 0x" << std::hex << std::uppercase << key;
  return stream.str();
}

std::string DescribeMessageRatePort(const std::uint32_t key)
{
  switch (key)
  {
    case kMsgoutUbxNavPvtUart1:
    case kMsgoutUbxNavSatUart1:
    case kMsgoutUbxNavStatusUart1:
    case kMsgoutUbxNavDopUart1:
    case kMsgoutUbxMonHwUart1:
    case kMsgoutUbxMonHw2Uart1:
    case kMsgoutUbxMonRfUart1:
    case kMsgoutUbxRxmRtcmUart1:
    case kMsgoutNmeaGgaUart1:
      return "UART1";
    case kMsgoutUbxNavPvtUart2:
    case kMsgoutUbxNavSatUart2:
    case kMsgoutUbxNavStatusUart2:
    case kMsgoutUbxNavDopUart2:
    case kMsgoutUbxMonHwUart2:
    case kMsgoutUbxMonHw2Uart2:
    case kMsgoutUbxMonRfUart2:
    case kMsgoutUbxRxmRtcmUart2:
    case kMsgoutNmeaGgaUart2:
      return "UART2";
    case kMsgoutUbxNavPvtUsb:
    case kMsgoutUbxNavSatUsb:
    case kMsgoutUbxNavStatusUsb:
    case kMsgoutUbxNavDopUsb:
    case kMsgoutUbxMonHwUsb:
    case kMsgoutUbxMonHw2Usb:
    case kMsgoutUbxMonRfUsb:
    case kMsgoutUbxRxmRtcmUsb:
    case kMsgoutNmeaGgaUsb:
      return "USB";
  }

  return "receiver port";
}

std::string DescribeConstellationKey(const std::uint32_t key)
{
  switch (key)
  {
    case kSignalGpsEnable:
      return "GPS";
    case kSignalGalEnable:
      return "Galileo";
    case kSignalBdsEnable:
      return "BeiDou";
    case kSignalGloEnable:
      return "GLONASS";
  }

  std::ostringstream stream;
  stream << "constellation key 0x" << std::hex << std::uppercase << key;
  return stream.str();
}

std::size_t UbxCfgValueSizeFromKey(const std::uint32_t key)
{
  switch ((key >> 28u) & 0x07u)
  {
    case 0x01u:
    case 0x02u:
      return 1u;
    case 0x03u:
      return 2u;
    case 0x04u:
      return 4u;
    default:
      return 0u;
  }
}

std::string DescribeUbxValset(const std::vector<std::uint8_t>& frame)
{
  if (frame.size() < 6u + 4u + 5u + 2u || frame[0] != 0xB5u || frame[1] != 0x62u ||
      frame[2] != kUbxCfgClass || frame[3] != kUbxCfgValsetId)
  {
    return "preview UBX binary configuration command";
  }

  const std::uint16_t payload_length = ReadLeU2(frame, 4u);
  if (frame.size() != static_cast<std::size_t>(payload_length) + 8u || payload_length < 9u)
  {
    return "preview UBX binary configuration command";
  }

  const std::size_t payload_offset = 6u;
  const std::uint32_t key = ReadLeU4(frame, payload_offset + 4u);
  const std::size_t value_size = UbxCfgValueSizeFromKey(key);
  if (value_size == 0u || payload_length != 8u + value_size)
  {
    return "preview UBX binary configuration command";
  }

  if ((key == kUart1Baudrate || key == kUart2Baudrate) && value_size == 4u)
  {
    return std::string("set ") + (key == kUart1Baudrate ? "UART1" : "UART2") + " baud rate to " +
           std::to_string(ReadLeU4(frame, payload_offset + 8u));
  }

  if (key == kRateMeas && value_size == 2u)
  {
    const std::uint16_t period_ms = ReadLeU2(frame, payload_offset + 8u);
    if (period_ms == 0u)
    {
      return "set measurement rate";
    }
    return "set measurement rate to " +
           FormatCompactDouble(1000.0 / static_cast<double>(period_ms), 3) + " Hz";
  }

  if ((key == kMsgoutUbxNavPvtUart1 || key == kMsgoutUbxNavPvtUart2 || key == kMsgoutUbxNavPvtUsb ||
       key == kMsgoutUbxNavSatUart1 || key == kMsgoutUbxNavSatUart2 || key == kMsgoutUbxNavSatUsb ||
       key == kMsgoutUbxNavStatusUart1 || key == kMsgoutUbxNavStatusUart2 ||
       key == kMsgoutUbxNavStatusUsb || key == kMsgoutUbxNavDopUart1 ||
       key == kMsgoutUbxNavDopUart2 || key == kMsgoutUbxNavDopUsb || key == kMsgoutUbxMonHwUart1 ||
       key == kMsgoutUbxMonHwUart2 || key == kMsgoutUbxMonHwUsb || key == kMsgoutUbxMonHw2Uart1 ||
       key == kMsgoutUbxMonHw2Uart2 || key == kMsgoutUbxMonHw2Usb || key == kMsgoutUbxMonRfUart1 ||
       key == kMsgoutUbxMonRfUart2 || key == kMsgoutUbxMonRfUsb || key == kMsgoutUbxRxmRtcmUart1 ||
       key == kMsgoutUbxRxmRtcmUart2 || key == kMsgoutUbxRxmRtcmUsb || key == kMsgoutNmeaGgaUart1 ||
       key == kMsgoutNmeaGgaUart2 || key == kMsgoutNmeaGgaUsb) &&
      value_size == 1u)
  {
    const std::uint8_t rate = frame[payload_offset + 8u];
    return std::string(rate == 0u ? "disable " : "enable ") + DescribeMessageRateKey(key) +
           (rate == 0u ? " output on " + DescribeMessageRatePort(key)
                       : " output on " + DescribeMessageRatePort(key) + " at rate " +
                             std::to_string(rate));
  }

  if ((key == kSignalGpsEnable || key == kSignalGalEnable || key == kSignalBdsEnable ||
       key == kSignalGloEnable) &&
      value_size == 1u)
  {
    const bool enabled = frame[payload_offset + 8u] != 0u;
    return std::string(enabled ? "enable " : "disable ") + DescribeConstellationKey(key) +
           " constellation";
  }

  std::ostringstream stream;
  stream << "apply UBX CFG key 0x" << std::hex << std::uppercase << key;
  return stream.str();
}

std::string DescribeUnicoreTextCommand(std::string text)
{
  text = TrimTrailingCrLf(std::move(text));

  if (text == "MODE ROVER")
  {
    return "set receiver mode to rover";
  }

  if (text == "MODE ROVER SURVEY MOW")
  {
    return "set receiver mode to rover survey lawn mower";
  }

  if (text == "MODE ROVER SURVEY")
  {
    return "set receiver mode to rover survey";
  }

  if (StartsWith(text, "CONFIG NMEA0183 "))
  {
    return "set NMEA0183 version to " + text.substr(std::string("CONFIG NMEA0183 ").size());
  }

  if (StartsWith(text, "CONFIG COM1 "))
  {
    return "set COM1 serial parameters to " + text.substr(std::string("CONFIG COM1 ").size());
  }

  if (StartsWith(text, "CONFIG RTK TIMEOUT "))
  {
    return "set RTK timeout to " + text.substr(std::string("CONFIG RTK TIMEOUT ").size()) + " s";
  }

  if (StartsWith(text, "CONFIG RTK RELIABILITY "))
  {
    return "set RTK reliability to " + text.substr(std::string("CONFIG RTK RELIABILITY ").size());
  }

  if (StartsWith(text, "CONFIG DGPS TIMEOUT "))
  {
    return "set DGPS timeout to " + text.substr(std::string("CONFIG DGPS TIMEOUT ").size()) + " s";
  }

  if (StartsWith(text, "CONFIG SIGNALGROUP "))
  {
    return "configure signal groups " + text.substr(std::string("CONFIG SIGNALGROUP ").size());
  }

  if (text == "SAVECONFIG")
  {
    return "save configuration to non-volatile memory";
  }

  if (text == "FRESET")
  {
    return "factory reset receiver state and restart at 115200 bps";
  }

  const auto onchanged_position = text.find(" ONCHANGED");
  if (onchanged_position != std::string::npos)
  {
    const std::string_view prefix(text.c_str(), onchanged_position);
    const auto port_separator = prefix.find_last_of(' ');
    if (port_separator != std::string::npos)
    {
      const std::string_view message = prefix.substr(0u, port_separator);
      const std::string_view port = prefix.substr(port_separator + 1u);
      if (LooksLikeUnicorePortToken(port))
      {
        return "enable " + std::string(message) + " output on " + std::string(port) +
               " when it changes";
      }
    }

    return "enable " + text.substr(0u, onchanged_position) + " output on change";
  }

  const auto last_space = text.find_last_of(' ');
  if (last_space != std::string::npos && last_space + 1u < text.size() &&
      std::isdigit(static_cast<unsigned char>(text[last_space + 1u])) != 0)
  {
    const std::string_view prefix(text.c_str(), last_space);
    const std::string_view period(text.c_str() + last_space + 1u, text.size() - last_space - 1u);
    const auto port_separator = prefix.find_last_of(' ');
    if (port_separator != std::string::npos)
    {
      const std::string_view message = prefix.substr(0u, port_separator);
      const std::string_view port = prefix.substr(port_separator + 1u);
      if (LooksLikeUnicorePortToken(port))
      {
        return "enable " + std::string(message) + " output on " + std::string(port) + " every " +
               std::string(period) + " s";
      }
    }

    return "enable " + std::string(prefix) + " output every " + std::string(period) + " s";
  }

  if (StartsWith(text, "LOG "))
  {
    const std::string_view remainder(text.c_str() + 4u, text.size() - 4u);
    const auto ontime_position = remainder.find(" ONTIME ");
    if (ontime_position != std::string_view::npos)
    {
      const std::string message(remainder.substr(0u, ontime_position));
      const std::string period(
          remainder.substr(ontime_position + std::string_view(" ONTIME ").size()));
      return "enable " + message + " output every " + period + " s";
    }
  }

  return "preview text configuration command";
}

std::string DescribeCommand(const ReceiverCommand& command)
{
  if (command.payload.kind == ReceiverCommandPayloadKind::kText)
  {
    return DescribeUnicoreTextCommand(command.payload.text);
  }

  if (command.payload.kind == ReceiverCommandPayloadKind::kBinary)
  {
    return DescribeUbxValset(command.payload.binary);
  }

  return "preview configuration command";
}

ProfilePreviewResult MakeErrorResult(const ProfilePreviewOptions& options,
                                     const ProfilePreviewStatus status,
                                     const std::string& error_message)
{
  ProfilePreviewResult result;
  result.status = status;
  result.vendor = ToLowerCopy(options.vendor);
  result.receiver_model = options.receiver_model;
  result.profile = ToLowerCopy(options.profile);
  result.persistent = options.persistent;
  result.signal_profile = options.signal_profile;
  result.signal_group_override = options.signal_group_override;
  result.output_port = options.output_port;
  result.baud = options.baud;
  result.rate_hz = options.rate_hz;
  result.error_message = error_message;
  return result;
}

void SummarizeCommand(ProfilePreviewResult& result, const ReceiverCommand& command)
{
  switch (command.safety_level)
  {
    case ReceiverCommandSafetyLevel::kRuntime:
      ++result.summary.runtime_commands;
      break;
    case ReceiverCommandSafetyLevel::kPersistent:
      ++result.summary.persistent_commands;
      break;
    case ReceiverCommandSafetyLevel::kFactoryReset:
      ++result.summary.factory_reset_commands;
      break;
  }

  ProfilePreviewCommand preview_command;
  preview_command.command = command;
  preview_command.payload_bytes = CommandPayloadSize(command);
  preview_command.description = DescribeCommand(command);
  result.commands.push_back(std::move(preview_command));
}

void FinalizeSummary(ProfilePreviewResult& result)
{
  result.summary.commands_total = result.commands.size();
}

std::optional<ReceiverDetectedFamily> ParseReceiverFamily(const std::string& vendor)
{
  const std::string normalized = ToLowerCopy(vendor);
  if (normalized == "ublox")
  {
    return ReceiverDetectedFamily::kUblox;
  }
  if (normalized == "unicore")
  {
    return ReceiverDetectedFamily::kUnicore;
  }
  if (normalized == "nmea")
  {
    return ReceiverDetectedFamily::kNmea;
  }
  return std::nullopt;
}

ProfilePreviewStatus MapPlanStatus(const ReceiverAutoConfigPlanStatus status)
{
  switch (status)
  {
    case ReceiverAutoConfigPlanStatus::kOk:
      return ProfilePreviewStatus::kOk;
    case ReceiverAutoConfigPlanStatus::kInvalidArgument:
      return ProfilePreviewStatus::kInvalidArgument;
    case ReceiverAutoConfigPlanStatus::kUnsupportedReceiver:
      return ProfilePreviewStatus::kUnsupportedVendor;
    case ReceiverAutoConfigPlanStatus::kUnsupportedProfile:
    case ReceiverAutoConfigPlanStatus::kUnsupportedApplyMode:
      return ProfilePreviewStatus::kUnsupportedProfile;
    case ReceiverAutoConfigPlanStatus::kBuildError:
      return ProfilePreviewStatus::kBuildError;
  }

  return ProfilePreviewStatus::kBuildError;
}

std::string SelectPlanErrorMessage(const ReceiverAutoConfigPlan& plan)
{
  if (!plan.error_message.empty())
  {
    return plan.error_message;
  }
  if (!plan.unsupported_reason.empty())
  {
    return plan.unsupported_reason;
  }
  return "preview planning failed";
}

}  // namespace

ProfilePreviewResult BuildProfilePreview(const ProfilePreviewOptions& options)
{
  if (options.vendor.empty() || options.profile.empty())
  {
    return MakeErrorResult(options,
                           ProfilePreviewStatus::kInvalidArgument,
                           "both vendor and profile are required");
  }

  const auto family = ParseReceiverFamily(options.vendor);
  if (!family.has_value())
  {
    return MakeErrorResult(options, ProfilePreviewStatus::kUnsupportedVendor, "unsupported vendor");
  }

  const auto profile = universal_gnss_driver::ParseReceiverAutoConfigProfile(options.profile);
  if (!profile.has_value())
  {
    return MakeErrorResult(options,
                           ProfilePreviewStatus::kUnsupportedProfile,
                           "unsupported configuration profile");
  }

  universal_gnss_driver::ReceiverAutoConfigRequest request;
  request.receiver_family = *family;
  request.requested_profile = *profile;
  request.apply_mode = options.persistent ? ReceiverAutoConfigApplyMode::kPersistent
                                          : ReceiverAutoConfigApplyMode::kRuntimeOnly;
  request.receiver_model = options.receiver_model;
  request.signal_profile = options.signal_profile;
  request.signal_group_override = options.signal_group_override;
  request.output_port = options.output_port;
  request.config_baud = options.baud;
  request.rate_hz = options.rate_hz;

  const auto plan = universal_gnss_driver::BuildReceiverAutoConfigPlan(request);

  ProfilePreviewResult result;
  result.status = MapPlanStatus(plan.status);
  result.vendor = ToLowerCopy(options.vendor);
  result.receiver_family = plan.receiver_family_name;
  result.receiver_model = plan.receiver_model;
  result.profile = plan.status == ReceiverAutoConfigPlanStatus::kOk
                       ? universal_gnss_driver::ToString(plan.request.requested_profile)
                       : ToLowerCopy(options.profile);
  result.persistent = options.persistent;
  result.signal_profile = plan.request.signal_profile;
  result.signal_group_override = plan.request.signal_group_override;
  result.output_port = plan.request.output_port;
  result.resolved_output_port = plan.resolved_output_port;
  result.baud = options.baud;
  result.rate_hz = options.rate_hz;
  result.warnings = plan.warnings;
  result.error_message = plan.status == ReceiverAutoConfigPlanStatus::kOk
                             ? std::string{}
                             : SelectPlanErrorMessage(plan);

  if (plan.status != ReceiverAutoConfigPlanStatus::kOk)
  {
    return result;
  }

  for (const auto& command : plan.commands)
  {
    SummarizeCommand(result, command);
  }
  FinalizeSummary(result);
  return result;
}

std::string DescribeProfilePreviewCommand(const ReceiverCommand& command)
{
  return DescribeCommand(command);
}

std::string FormatProfilePreviewText(const ProfilePreviewResult& result, const bool verbose)
{
  std::ostringstream output;
  if (result.status != ProfilePreviewStatus::kOk)
  {
    output << "Error: " << result.error_message << '\n';
    return output.str();
  }

  output << "Profile: " << result.vendor << ' ' << result.profile << "\n";
  if (!result.receiver_family.empty())
  {
    output << "Receiver family: " << result.receiver_family << "\n";
  }
  if (result.receiver_model.has_value())
  {
    output << "Receiver model: " << *result.receiver_model << "\n";
  }
  output << "Preview: offline only, no receiver communication\n";
  if (result.persistent)
  {
    output << "Persistence: enabled\n";
  }
  if (result.signal_profile.has_value())
  {
    output << "Signal profile override: " << universal_gnss_driver::ToString(*result.signal_profile)
           << "\n";
  }
  if (result.signal_group_override.has_value())
  {
    output << "Signal-group override: "
           << universal_gnss_driver::FormatUnicoreSignalGroupSelection(
                  *result.signal_group_override)
           << "\n";
  }
  if (result.vendor == "ublox")
  {
    if (!result.output_port.has_value())
    {
      output << "Output port: legacy_default (uart1 + usb)\n";
    }
    else if (*result.output_port == universal_gnss_driver::ReceiverAutoConfigOutputPort::kAuto)
    {
      output << "Output port request: auto\n";
      if (result.resolved_output_port.has_value())
      {
        output << "Resolved output port: "
               << universal_gnss_driver::ToString(*result.resolved_output_port) << "\n";
      }
    }
    else
    {
      output << "Output port: "
             << universal_gnss_driver::ToString(
                    result.resolved_output_port.value_or(*result.output_port))
             << "\n";
    }
  }
  if (result.baud.has_value())
  {
    output << "Config baud override: " << *result.baud << "\n";
  }
  if (HasFactoryResetCommand(result.commands))
  {
    output << "Factory reset baud: 115200\n";
  }
  if (const auto target_baud = ExtractPlannedUnicoreConfigBaud(result.commands);
      target_baud.has_value())
  {
    output << "Target configured baud: " << *target_baud << "\n";
  }
  if (result.rate_hz.has_value())
  {
    output << "Rate override: " << FormatCompactDouble(*result.rate_hz, 3) << " Hz\n";
  }
  output << '\n';

  for (std::size_t index = 0; index < result.commands.size(); ++index)
  {
    const auto& preview_command = result.commands[index];
    output << "Command " << (index + 1u) << ":\n";
    output << "  kind: " << CommandKindToString(preview_command.command.kind) << "\n";
    output << "  safety: " << SafetyLevelToString(preview_command.command.safety_level) << "\n";
    output << "  payload: " << PayloadKindToString(preview_command.command.payload.kind) << "\n";
    output << "  bytes: " << preview_command.payload_bytes << "\n";
    output << "  description: " << preview_command.description << "\n";

    if (preview_command.command.payload.kind == ReceiverCommandPayloadKind::kText)
    {
      output << "  command: " << TrimTrailingCrLf(preview_command.command.payload.text) << "\n";
    }
    else if (verbose && preview_command.command.payload.kind == ReceiverCommandPayloadKind::kBinary)
    {
      output << "  hex: " << FormatHexBytes(preview_command.command.payload.binary) << "\n";
    }

    output << '\n';
  }

  if (!result.warnings.empty())
  {
    output << "Warnings:\n";
    for (const auto& warning : result.warnings)
    {
      output << "- " << warning << "\n";
    }
    output << '\n';
  }

  output << "Summary:\n";
  output << "  commands: " << result.summary.commands_total << "\n";
  output << "  runtime: " << result.summary.runtime_commands << "\n";
  output << "  persistent: " << result.summary.persistent_commands << "\n";
  output << "  factory_reset: " << result.summary.factory_reset_commands << "\n";
  return output.str();
}

std::string FormatProfilePreviewJson(const ProfilePreviewResult& result, const bool verbose)
{
  std::ostringstream output;
  output << "{\n";
  output << "  \"status\": \"" << (result.status == ProfilePreviewStatus::kOk ? "ok" : "error")
         << "\",\n";
  output << "  \"preview_only\": true,\n";
  output << "  \"vendor\": \"" << EscapeJson(result.vendor) << "\",\n";
  output << "  \"receiver_family\": \"" << EscapeJson(result.receiver_family) << "\",\n";
  output << "  \"receiver_model\": ";
  if (result.receiver_model.has_value())
  {
    output << "\"" << EscapeJson(*result.receiver_model) << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "  \"profile\": \"" << EscapeJson(result.profile) << "\",\n";
  output << "  \"persistent\": " << (result.persistent ? "true" : "false") << ",\n";
  output << "  \"signal_profile\": ";
  if (result.signal_profile.has_value())
  {
    output << "\"" << EscapeJson(universal_gnss_driver::ToString(*result.signal_profile)) << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "  \"signal_group_override\": ";
  if (result.signal_group_override.has_value())
  {
    output << "\""
           << EscapeJson(universal_gnss_driver::FormatUnicoreSignalGroupSelection(
                  *result.signal_group_override))
           << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "  \"output_port\": ";
  if (result.output_port.has_value())
  {
    output << "\"" << EscapeJson(universal_gnss_driver::ToString(*result.output_port)) << "\"";
  }
  else if (result.vendor == "ublox")
  {
    output << "\"legacy_default\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "  \"resolved_output_port\": ";
  if (result.resolved_output_port.has_value())
  {
    output << "\"" << EscapeJson(universal_gnss_driver::ToString(*result.resolved_output_port))
           << "\"";
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "  \"baud\": ";
  if (result.baud.has_value())
  {
    output << *result.baud;
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "  \"target_configured_baud\": ";
  if (const auto target_baud = ExtractPlannedUnicoreConfigBaud(result.commands);
      target_baud.has_value())
  {
    output << *target_baud;
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "  \"factory_reset_baud\": ";
  if (HasFactoryResetCommand(result.commands))
  {
    output << 115200u;
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "  \"rate_hz\": ";
  if (result.rate_hz.has_value())
  {
    output << FormatCompactDouble(*result.rate_hz, 6);
  }
  else
  {
    output << "null";
  }
  output << ",\n";
  output << "  \"error_message\": \"" << EscapeJson(result.error_message) << "\",\n";
  output << "  \"warnings\": [\n";
  for (std::size_t index = 0; index < result.warnings.size(); ++index)
  {
    output << "    \"" << EscapeJson(result.warnings[index]) << "\"";
    if (index + 1u != result.warnings.size())
    {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"commands\": [\n";

  for (std::size_t index = 0; index < result.commands.size(); ++index)
  {
    const auto& preview_command = result.commands[index];
    output << "    {\n";
    output << "      \"index\": " << (index + 1u) << ",\n";
    output << "      \"kind\": \"" << CommandKindToString(preview_command.command.kind) << "\",\n";
    output << "      \"safety\": \"" << SafetyLevelToString(preview_command.command.safety_level)
           << "\",\n";
    output << "      \"payload_kind\": \""
           << PayloadKindToString(preview_command.command.payload.kind) << "\",\n";
    output << "      \"bytes\": " << preview_command.payload_bytes << ",\n";
    output << "      \"description\": \"" << EscapeJson(preview_command.description) << "\"";

    if (preview_command.command.payload.kind == ReceiverCommandPayloadKind::kText)
    {
      output << ",\n      \"command\": \""
             << EscapeJson(TrimTrailingCrLf(preview_command.command.payload.text)) << "\"";
    }
    else if (verbose && preview_command.command.payload.kind == ReceiverCommandPayloadKind::kBinary)
    {
      output << ",\n      \"hex\": \""
             << EscapeJson(FormatHexBytes(preview_command.command.payload.binary)) << "\"";
    }
    output << "\n    }";
    if (index + 1u != result.commands.size())
    {
      output << ",";
    }
    output << "\n";
  }

  output << "  ],\n";
  output << "  \"summary\": {\n";
  output << "    \"commands\": " << result.summary.commands_total << ",\n";
  output << "    \"runtime\": " << result.summary.runtime_commands << ",\n";
  output << "    \"persistent\": " << result.summary.persistent_commands << ",\n";
  output << "    \"factory_reset\": " << result.summary.factory_reset_commands << "\n";
  output << "  }\n";
  output << "}\n";
  return output.str();
}

}  // namespace universal_gnss_tools
