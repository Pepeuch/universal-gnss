#include "universal_gnss_protocols/unicore_parser.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace universal_gnss_protocols
{

namespace
{

enum class OptionalFieldStatus
{
  kMissing = 0,
  kValue = 1,
  kInvalid = 2,
};

template <std::size_t MaxFields>
bool TokenizeCsv(std::string_view text,
                 std::array<std::string_view, MaxFields>& fields,
                 std::size_t& field_count)
{
  field_count = 0;
  std::size_t start = 0;

  while (start <= text.size())
  {
    if (field_count >= MaxFields)
    {
      return false;
    }

    const std::size_t comma = text.find(',', start);
    if (comma == std::string_view::npos)
    {
      fields[field_count++] = text.substr(start);
      break;
    }

    fields[field_count++] = text.substr(start, comma - start);
    start = comma + 1u;

    if (start == text.size())
    {
      if (field_count >= MaxFields)
      {
        return false;
      }
      fields[field_count++] = std::string_view{};
      break;
    }
  }

  return true;
}

std::string_view TrimField(std::string_view text)
{
  while (!text.empty() &&
         (text.front() == ' ' || text.front() == '\t' || text.front() == '"'))
  {
    text.remove_prefix(1u);
  }

  while (!text.empty() &&
         (text.back() == ' ' || text.back() == '\t' || text.back() == '"'))
  {
    text.remove_suffix(1u);
  }

  return text;
}

bool TryParseUnsigned(std::string_view text, unsigned int& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    return false;
  }

  std::string buffer(text);
  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(buffer.c_str(), &end, 10);
  if (errno != 0 || end == nullptr || *end != '\0' ||
      parsed > std::numeric_limits<unsigned int>::max())
  {
    return false;
  }

  value = static_cast<unsigned int>(parsed);
  return true;
}

bool LooksUnsignedIntegerField(std::string_view text)
{
  text = TrimField(text);
  if (text.empty())
  {
    return false;
  }

  return text.find_first_of(".eE") == std::string_view::npos;
}

bool TryParseUnsignedAutoBase(std::string_view text, unsigned int& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    return false;
  }

  std::string buffer(text);
  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(buffer.c_str(), &end, 0);
  if (errno != 0 || end == nullptr || *end != '\0' ||
      parsed > std::numeric_limits<unsigned int>::max())
  {
    return false;
  }

  value = static_cast<unsigned int>(parsed);
  return true;
}

bool TryParseUnsignedBase(std::string_view text, const int base, unsigned int& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    return false;
  }

  std::string buffer(text);
  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(buffer.c_str(), &end, base);
  if (errno != 0 || end == nullptr || *end != '\0' ||
      parsed > std::numeric_limits<unsigned int>::max())
  {
    return false;
  }

  value = static_cast<unsigned int>(parsed);
  return true;
}

bool TryParseSigned(std::string_view text, int& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    return false;
  }

  std::string buffer(text);
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(buffer.c_str(), &end, 10);
  if (errno != 0 || end == nullptr || *end != '\0' ||
      parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max())
  {
    return false;
  }

  value = static_cast<int>(parsed);
  return true;
}

bool TryParseDouble(std::string_view text, double& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    return false;
  }

  std::string buffer(text);
  char* end = nullptr;
  errno = 0;
  const double parsed = std::strtod(buffer.c_str(), &end);
  if (errno != 0 || end == nullptr || *end != '\0' || !std::isfinite(parsed))
  {
    return false;
  }

  value = parsed;
  return true;
}

OptionalFieldStatus ParseOptionalFloat(std::string_view text, std::optional<float>& value)
{
  value.reset();
  text = TrimField(text);
  if (text.empty())
  {
    return OptionalFieldStatus::kMissing;
  }

  double parsed = 0.0;
  if (!TryParseDouble(text, parsed))
  {
    return OptionalFieldStatus::kInvalid;
  }

  value = static_cast<float>(parsed);
  return OptionalFieldStatus::kValue;
}

OptionalFieldStatus ParseOptionalDouble(std::string_view text, std::optional<double>& value)
{
  value.reset();
  text = TrimField(text);
  if (text.empty())
  {
    return OptionalFieldStatus::kMissing;
  }

  double parsed = 0.0;
  if (!TryParseDouble(text, parsed))
  {
    return OptionalFieldStatus::kInvalid;
  }

  value = parsed;
  return OptionalFieldStatus::kValue;
}

OptionalFieldStatus ParseOptionalUnsigned16(std::string_view text,
                                            std::optional<std::uint16_t>& value)
{
  value.reset();
  text = TrimField(text);
  if (text.empty())
  {
    return OptionalFieldStatus::kMissing;
  }

  unsigned int parsed = 0;
  if (!TryParseUnsigned(text, parsed) ||
      parsed > static_cast<unsigned int>(std::numeric_limits<std::uint16_t>::max()))
  {
    return OptionalFieldStatus::kInvalid;
  }

  value = static_cast<std::uint16_t>(parsed);
  return OptionalFieldStatus::kValue;
}

OptionalFieldStatus ParseOptionalUnsigned8(std::string_view text,
                                           std::optional<std::uint8_t>& value)
{
  value.reset();
  text = TrimField(text);
  if (text.empty())
  {
    return OptionalFieldStatus::kMissing;
  }

  unsigned int parsed = 0;
  if (!TryParseUnsigned(text, parsed) ||
      parsed > static_cast<unsigned int>(std::numeric_limits<std::uint8_t>::max()))
  {
    return OptionalFieldStatus::kInvalid;
  }

  value = static_cast<std::uint8_t>(parsed);
  return OptionalFieldStatus::kValue;
}

OptionalFieldStatus ParseOptionalUnsigned32(std::string_view text,
                                            std::optional<std::uint32_t>& value)
{
  value.reset();
  text = TrimField(text);
  if (text.empty())
  {
    return OptionalFieldStatus::kMissing;
  }

  unsigned int parsed = 0;
  if (!TryParseUnsigned(text, parsed))
  {
    return OptionalFieldStatus::kInvalid;
  }

  value = static_cast<std::uint32_t>(parsed);
  return OptionalFieldStatus::kValue;
}

OptionalFieldStatus ParseOptionalBoolEquals(std::string_view text,
                                            const std::string_view expected,
                                            std::optional<bool>& value)
{
  value.reset();
  text = TrimField(text);
  if (text.empty())
  {
    return OptionalFieldStatus::kMissing;
  }

  value = (text == expected);
  return OptionalFieldStatus::kValue;
}

OptionalFieldStatus ParseOptionalAgcRegister(std::string_view text,
                                             std::optional<std::int16_t>& value)
{
  value.reset();
  text = TrimField(text);
  if (text.empty())
  {
    return OptionalFieldStatus::kMissing;
  }

  int parsed = 0;
  if (!TryParseSigned(text, parsed))
  {
    return OptionalFieldStatus::kInvalid;
  }

  if (parsed == -1)
  {
    return OptionalFieldStatus::kMissing;
  }

  if (parsed < 0 || parsed > 255)
  {
    return OptionalFieldStatus::kInvalid;
  }

  value = static_cast<std::int16_t>(parsed);
  return OptionalFieldStatus::kValue;
}

std::uint16_t ReadLittleEndian16(const std::uint8_t* data)
{
  return static_cast<std::uint16_t>(data[0]) |
         (static_cast<std::uint16_t>(data[1]) << 8);
}

std::uint32_t ReadLittleEndian32(const std::uint8_t* data)
{
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) |
         (static_cast<std::uint32_t>(data[3]) << 24);
}

float ReadLittleEndianFloat32(const std::uint8_t* data)
{
  const std::uint32_t bits = ReadLittleEndian32(data);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

double ReadLittleEndianFloat64(const std::uint8_t* data)
{
  const std::uint64_t bits =
      static_cast<std::uint64_t>(data[0]) |
      (static_cast<std::uint64_t>(data[1]) << 8) |
      (static_cast<std::uint64_t>(data[2]) << 16) |
      (static_cast<std::uint64_t>(data[3]) << 24) |
      (static_cast<std::uint64_t>(data[4]) << 32) |
      (static_cast<std::uint64_t>(data[5]) << 40) |
      (static_cast<std::uint64_t>(data[6]) << 48) |
      (static_cast<std::uint64_t>(data[7]) << 56);
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

UnicoreTimeReference ParseTimeReference(std::string_view text)
{
  text = TrimField(text);
  if (text == "GPS" || text == "GPST")
  {
    return UnicoreTimeReference::kGps;
  }
  if (text == "BDS" || text == "BDS" || text == "BDT" || text == "BDST")
  {
    return UnicoreTimeReference::kBds;
  }
  return UnicoreTimeReference::kUnknown;
}

UnicoreTimeStatus ParseTimeStatus(std::string_view text)
{
  text = TrimField(text);
  if (text == "FINE")
  {
    return UnicoreTimeStatus::kFine;
  }
  return UnicoreTimeStatus::kUnknown;
}

UnicoreSolutionStatus ParseSolutionStatus(std::string_view text)
{
  text = TrimField(text);
  if (text == "SOL_COMPUTED")
  {
    return UnicoreSolutionStatus::kSolComputed;
  }
  if (text == "INSUFFICIENT_OBS")
  {
    return UnicoreSolutionStatus::kInsufficientObs;
  }
  if (text == "NO_CONVERGENCE")
  {
    return UnicoreSolutionStatus::kNoConvergence;
  }
  if (text == "COV_TRACE")
  {
    return UnicoreSolutionStatus::kCovTrace;
  }
  return UnicoreSolutionStatus::kUnknown;
}

UnicorePositionType ParsePositionType(std::string_view text)
{
  text = TrimField(text);
  if (text == "NONE")
  {
    return UnicorePositionType::kNone;
  }
  if (text == "FIXEDPOS")
  {
    return UnicorePositionType::kFixedPos;
  }
  if (text == "FIXEDHEIGHT")
  {
    return UnicorePositionType::kFixedHeight;
  }
  if (text == "DOPPLER_VELOCITY")
  {
    return UnicorePositionType::kDopplerVelocity;
  }
  if (text == "SINGLE")
  {
    return UnicorePositionType::kSingle;
  }
  if (text == "PSRDIFF")
  {
    return UnicorePositionType::kPsrDiff;
  }
  if (text == "SBAS")
  {
    return UnicorePositionType::kSbas;
  }
  if (text == "L1_FLOAT")
  {
    return UnicorePositionType::kL1Float;
  }
  if (text == "IONOFREE_FLOAT")
  {
    return UnicorePositionType::kIonoFreeFloat;
  }
  if (text == "NARROW_FLOAT")
  {
    return UnicorePositionType::kNarrowFloat;
  }
  if (text == "L1_INT")
  {
    return UnicorePositionType::kL1Int;
  }
  if (text == "WIDE_INT")
  {
    return UnicorePositionType::kWideInt;
  }
  if (text == "NARROW_INT")
  {
    return UnicorePositionType::kNarrowInt;
  }
  if (text == "INS")
  {
    return UnicorePositionType::kIns;
  }
  if (text == "INS_PSRSP")
  {
    return UnicorePositionType::kInsPsrsp;
  }
  if (text == "INS_PSRDIFF")
  {
    return UnicorePositionType::kInsPsrDiff;
  }
  if (text == "INS_RTKFLOAT")
  {
    return UnicorePositionType::kInsRtkFloat;
  }
  if (text == "INS_RTKFIXED")
  {
    return UnicorePositionType::kInsRtkFixed;
  }
  if (text == "PPP_CONVERGING")
  {
    return UnicorePositionType::kPppConverging;
  }
  if (text == "PPP")
  {
    return UnicorePositionType::kPpp;
  }
  return UnicorePositionType::kUnknown;
}

UnicoreDualAntennaStatus ParseDualAntennaStatus(std::string_view text)
{
  text = TrimField(text);
  unsigned int parsed = 0;
  if (!TryParseUnsigned(text, parsed))
  {
    return UnicoreDualAntennaStatus::kUnknown;
  }

  switch (parsed)
  {
    case 0u:
      return UnicoreDualAntennaStatus::kNotSolved;
    case 1u:
      return UnicoreDualAntennaStatus::kWithinLimit;
    case 2u:
      return UnicoreDualAntennaStatus::kOutOfLimit;
    case 255u:
      return UnicoreDualAntennaStatus::kNotConfigured;
    default:
      return UnicoreDualAntennaStatus::kUnknown;
  }
}

UnicoreJammingState ParseJammingState(const unsigned int raw_value)
{
  switch (raw_value)
  {
    case 0u:
      return UnicoreJammingState::kNone;
    case 1u:
      return UnicoreJammingState::kJamming;
    case 2u:
      return UnicoreJammingState::kStrongJamming;
    default:
      return UnicoreJammingState::kUnknown;
  }
}

UnicoreSatelliteConstellation ParseBestSatConstellation(std::string_view text)
{
  text = TrimField(text);
  if (text == "GPS")
  {
    return UnicoreSatelliteConstellation::kGps;
  }
  if (text == "GLONASS")
  {
    return UnicoreSatelliteConstellation::kGlonass;
  }
  if (text == "GALILEO")
  {
    return UnicoreSatelliteConstellation::kGalileo;
  }
  if (text == "BEIDOU" || text == "BDS")
  {
    return UnicoreSatelliteConstellation::kBeiDou;
  }
  if (text == "QZSS")
  {
    return UnicoreSatelliteConstellation::kQzss;
  }
  return UnicoreSatelliteConstellation::kUnknown;
}

std::uint32_t BestSatUsedSignalMask(const UnicoreSatelliteConstellation constellation)
{
  switch (constellation)
  {
    case UnicoreSatelliteConstellation::kGps:
    case UnicoreSatelliteConstellation::kGalileo:
    case UnicoreSatelliteConstellation::kQzss:
      return 0x0Fu;
    case UnicoreSatelliteConstellation::kGlonass:
    case UnicoreSatelliteConstellation::kBeiDou:
      return 0x07u;
    case UnicoreSatelliteConstellation::kUnknown:
    default:
      return 0x00u;
  }
}

bool ParseBestSatSatelliteId(std::string_view text,
                             std::uint16_t& satellite_id,
                             std::optional<std::int16_t>& glonass_frequency_channel)
{
  satellite_id = 0u;
  glonass_frequency_channel.reset();
  text = TrimField(text);
  if (text.empty())
  {
    return false;
  }

  const std::size_t suffix_offset = text.find_first_of("+-", 1u);
  const std::string_view satellite_text =
      suffix_offset == std::string_view::npos ? text : text.substr(0u, suffix_offset);

  unsigned int parsed_satellite_id = 0u;
  if (!TryParseUnsigned(satellite_text, parsed_satellite_id) ||
      parsed_satellite_id > std::numeric_limits<std::uint16_t>::max())
  {
    return false;
  }

  satellite_id = static_cast<std::uint16_t>(parsed_satellite_id);
  if (suffix_offset == std::string_view::npos)
  {
    return true;
  }

  unsigned int parsed_channel = 0u;
  if (suffix_offset + 1u >= text.size() ||
      !TryParseUnsigned(text.substr(suffix_offset + 1u), parsed_channel) ||
      parsed_channel > static_cast<unsigned int>(std::numeric_limits<std::int16_t>::max()))
  {
    return false;
  }

  const int signed_channel =
      text[suffix_offset] == '-' ? -static_cast<int>(parsed_channel)
                                 : static_cast<int>(parsed_channel);
  if (signed_channel < static_cast<int>(std::numeric_limits<std::int16_t>::min()) ||
      signed_channel > static_cast<int>(std::numeric_limits<std::int16_t>::max()))
  {
    return false;
  }

  glonass_frequency_channel = static_cast<std::int16_t>(signed_channel);
  return true;
}

UnicoreSolutionStatus ParseBinarySolutionStatus(const std::uint32_t raw_value)
{
  switch (raw_value)
  {
    case 0u:
      return UnicoreSolutionStatus::kSolComputed;
    case 1u:
      return UnicoreSolutionStatus::kInsufficientObs;
    case 2u:
      return UnicoreSolutionStatus::kNoConvergence;
    case 4u:
      return UnicoreSolutionStatus::kCovTrace;
    default:
      return UnicoreSolutionStatus::kUnknown;
  }
}

UnicorePositionType ParseBinaryPositionType(const std::uint32_t raw_value)
{
  switch (raw_value)
  {
    case 0u:
      return UnicorePositionType::kNone;
    case 1u:
      return UnicorePositionType::kFixedPos;
    case 2u:
      return UnicorePositionType::kFixedHeight;
    case 8u:
      return UnicorePositionType::kDopplerVelocity;
    case 16u:
      return UnicorePositionType::kSingle;
    case 17u:
      return UnicorePositionType::kPsrDiff;
    case 18u:
      return UnicorePositionType::kSbas;
    case 32u:
      return UnicorePositionType::kL1Float;
    case 33u:
      return UnicorePositionType::kIonoFreeFloat;
    case 34u:
      return UnicorePositionType::kNarrowFloat;
    case 48u:
      return UnicorePositionType::kL1Int;
    case 49u:
      return UnicorePositionType::kWideInt;
    case 50u:
      return UnicorePositionType::kNarrowInt;
    case 52u:
      return UnicorePositionType::kIns;
    case 53u:
      return UnicorePositionType::kInsPsrsp;
    case 54u:
      return UnicorePositionType::kInsPsrDiff;
    case 55u:
      return UnicorePositionType::kInsRtkFloat;
    case 56u:
      return UnicorePositionType::kInsRtkFixed;
    case 68u:
      return UnicorePositionType::kPppConverging;
    case 69u:
      return UnicorePositionType::kPpp;
    default:
      return UnicorePositionType::kUnknown;
  }
}

template <typename RecordT>
ParserResult<RecordT> InvalidResult()
{
  return ParserResult<RecordT>::InvalidData();
}

struct ParsedUnicoreMessage
{
  UnicoreAsciiHeader header{};
  std::string_view body{};
};

template <std::size_t MaxHeaderFields>
std::optional<ParsedUnicoreMessage> ParseAsciiHeader(const UnicoreFrame& frame,
                                                     const std::string_view expected_name)
{
  if (frame.protocol != ProtocolType::kUnicore || frame.sync_char != '#')
  {
    return std::nullopt;
  }

  if (frame.checksum_status != ChecksumStatus::kValid)
  {
    return std::nullopt;
  }

  if (frame.message_name != expected_name)
  {
    return std::nullopt;
  }

  const std::string_view payload_text(
      reinterpret_cast<const char*>(frame.payload.data()),
      frame.payload.size());
  const std::size_t semicolon = payload_text.find(';');
  if (semicolon == std::string_view::npos)
  {
    return std::nullopt;
  }

  const std::size_t star = payload_text.find('*', semicolon + 1u);
  const std::string_view header_text = payload_text.substr(0, semicolon);
  const std::string_view body_text =
      payload_text.substr(semicolon + 1u,
                          star == std::string_view::npos ? std::string_view::npos
                                                         : (star - semicolon - 1u));

  std::array<std::string_view, MaxHeaderFields> header_fields{};
  std::size_t header_field_count = 0u;
  if (!TokenizeCsv(header_text, header_fields, header_field_count) || header_field_count != 10u)
  {
    return std::nullopt;
  }

  if (TrimField(header_fields[0]) != expected_name)
  {
    return std::nullopt;
  }

  unsigned int cpu_idle = 0u;
  unsigned int week = 0u;
  unsigned int tow_ms = 0u;
  unsigned int format_version = 0u;
  unsigned int leap_seconds = 0u;
  unsigned int output_delay = 0u;
  if (!TryParseUnsigned(header_fields[1], cpu_idle) ||
      !TryParseUnsigned(header_fields[4], week) ||
      !TryParseUnsigned(header_fields[5], tow_ms) ||
      !TryParseUnsigned(header_fields[6], format_version) ||
      !TryParseUnsigned(header_fields[8], leap_seconds) ||
      !TryParseUnsigned(header_fields[9], output_delay) ||
      cpu_idle > std::numeric_limits<std::uint8_t>::max() ||
      week > std::numeric_limits<std::uint16_t>::max() ||
      leap_seconds > std::numeric_limits<std::uint8_t>::max() ||
      output_delay > std::numeric_limits<std::uint16_t>::max())
  {
    return std::nullopt;
  }

  ParsedUnicoreMessage parsed;
  parsed.header.timestamp_ns = frame.timestamp_ns;
  parsed.header.cpu_idle_percent = static_cast<std::uint8_t>(cpu_idle);
  parsed.header.time_reference = ParseTimeReference(header_fields[2]);
  parsed.header.time_status = ParseTimeStatus(header_fields[3]);
  parsed.header.gps_week = static_cast<std::uint16_t>(week);
  parsed.header.gps_millis_of_week = static_cast<std::uint32_t>(tow_ms);
  parsed.header.format_version = static_cast<std::uint32_t>(format_version);
  parsed.header.leap_seconds = static_cast<std::uint8_t>(leap_seconds);
  parsed.header.output_delay_ms = static_cast<std::uint16_t>(output_delay);
  parsed.body = body_text;
  return parsed;
}

template <std::size_t MaxFields>
bool TokenizeBody(std::string_view body,
                  std::array<std::string_view, MaxFields>& fields,
                  std::size_t& field_count)
{
  return TokenizeCsv(body, fields, field_count);
}

bool ConsumeCsvField(std::string_view& text, std::string_view& field)
{
  if (text.empty())
  {
    field = std::string_view{};
    return false;
  }

  const std::size_t comma = text.find(',');
  if (comma == std::string_view::npos)
  {
    field = text;
    text = std::string_view{};
    return true;
  }

  field = text.substr(0, comma);
  text.remove_prefix(comma + 1u);
  return true;
}

universal_gnss::GnssFixType MapFixType(const UnicorePositionType type)
{
  switch (type)
  {
    case UnicorePositionType::kNone:
      return universal_gnss::GnssFixType::kNoFix;
    case UnicorePositionType::kL1Float:
    case UnicorePositionType::kIonoFreeFloat:
    case UnicorePositionType::kNarrowFloat:
    case UnicorePositionType::kInsRtkFloat:
      return universal_gnss::GnssFixType::kRtkFloat;
    case UnicorePositionType::kL1Int:
    case UnicorePositionType::kWideInt:
    case UnicorePositionType::kNarrowInt:
    case UnicorePositionType::kInsRtkFixed:
      return universal_gnss::GnssFixType::kRtkFixed;
    case UnicorePositionType::kIns:
      return universal_gnss::GnssFixType::kDeadReckoning;
    case UnicorePositionType::kFixedPos:
    case UnicorePositionType::kFixedHeight:
    case UnicorePositionType::kDopplerVelocity:
    case UnicorePositionType::kSingle:
    case UnicorePositionType::kPsrDiff:
    case UnicorePositionType::kSbas:
    case UnicorePositionType::kInsPsrsp:
    case UnicorePositionType::kInsPsrDiff:
    case UnicorePositionType::kPppConverging:
    case UnicorePositionType::kPpp:
      return universal_gnss::GnssFixType::kFix;
    case UnicorePositionType::kUnknown:
    default:
      return universal_gnss::GnssFixType::kUnknown;
  }
}

std::optional<universal_gnss::GnssRtkMode> MapRtkMode(const UnicorePositionType type)
{
  switch (type)
  {
    case UnicorePositionType::kL1Float:
    case UnicorePositionType::kIonoFreeFloat:
    case UnicorePositionType::kNarrowFloat:
    case UnicorePositionType::kInsRtkFloat:
      return universal_gnss::GnssRtkMode::kFloat;
    case UnicorePositionType::kL1Int:
    case UnicorePositionType::kWideInt:
    case UnicorePositionType::kNarrowInt:
    case UnicorePositionType::kInsRtkFixed:
      return universal_gnss::GnssRtkMode::kFixed;
    case UnicorePositionType::kNone:
    case UnicorePositionType::kFixedPos:
    case UnicorePositionType::kFixedHeight:
    case UnicorePositionType::kDopplerVelocity:
    case UnicorePositionType::kSingle:
    case UnicorePositionType::kPsrDiff:
    case UnicorePositionType::kSbas:
    case UnicorePositionType::kIns:
    case UnicorePositionType::kInsPsrsp:
    case UnicorePositionType::kInsPsrDiff:
    case UnicorePositionType::kPppConverging:
    case UnicorePositionType::kPpp:
      return universal_gnss::GnssRtkMode::kNone;
    case UnicorePositionType::kUnknown:
    default:
      return std::nullopt;
  }
}

std::optional<bool> MapDifferentialCorrections(const UnicorePositionType type)
{
  switch (type)
  {
    case UnicorePositionType::kPsrDiff:
    case UnicorePositionType::kSbas:
    case UnicorePositionType::kL1Float:
    case UnicorePositionType::kIonoFreeFloat:
    case UnicorePositionType::kNarrowFloat:
    case UnicorePositionType::kL1Int:
    case UnicorePositionType::kWideInt:
    case UnicorePositionType::kNarrowInt:
    case UnicorePositionType::kInsPsrDiff:
    case UnicorePositionType::kInsRtkFloat:
    case UnicorePositionType::kInsRtkFixed:
      return true;
    case UnicorePositionType::kNone:
    case UnicorePositionType::kFixedPos:
    case UnicorePositionType::kFixedHeight:
    case UnicorePositionType::kDopplerVelocity:
    case UnicorePositionType::kSingle:
    case UnicorePositionType::kIns:
    case UnicorePositionType::kInsPsrsp:
      return false;
    case UnicorePositionType::kPppConverging:
    case UnicorePositionType::kPpp:
    case UnicorePositionType::kUnknown:
    default:
      return std::nullopt;
  }
}

std::optional<bool> MapCorrectionsActive(const UnicorePositionType type)
{
  return MapDifferentialCorrections(type);
}

void ApplyFixType(universal_gnss::GnssRuntimeState& state, const UnicorePositionType type)
{
  const auto fix_type = MapFixType(type);
  state.fix_type = fix_type;
  state.fix_valid =
      fix_type != universal_gnss::GnssFixType::kUnknown &&
      fix_type != universal_gnss::GnssFixType::kNoFix;
}

void ApplyRtkMode(universal_gnss::GnssRuntimeState& state, const UnicorePositionType type)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kRtkMode);
  if (const auto rtk_mode = MapRtkMode(type); rtk_mode.has_value())
  {
    universal_gnss::SetOptionalValue(
        state, universal_gnss::GnssCapability::kRtkMode, state.rtk_mode, *rtk_mode);
  }
}

void ApplyCorrectionState(universal_gnss::GnssRuntimeState& state, const UnicorePositionType type)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kDifferentialCorrections);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kCorrectionsActive);

  if (const auto differential_corrections = MapDifferentialCorrections(type);
      differential_corrections.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kDifferentialCorrections,
                                     state.differential_corrections,
                                     *differential_corrections);
  }
  if (const auto corrections_active = MapCorrectionsActive(type); corrections_active.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kCorrectionsActive,
                                     state.corrections_active,
                                     *corrections_active);
  }
}

void SetHorizontalAccuracyFromSigmas(universal_gnss::GnssRuntimeState& state,
                                     const std::optional<float> first_sigma,
                                     const std::optional<float> second_sigma)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHorizontalAccuracy);
  if (first_sigma.has_value() && second_sigma.has_value())
  {
    const float horizontal_accuracy = std::max(*first_sigma, *second_sigma);
    universal_gnss::SetOptionalValue(
        state,
        universal_gnss::GnssCapability::kHorizontalAccuracy,
        state.horizontal_accuracy_m,
        horizontal_accuracy);
  }
}

void SetVerticalAccuracy(universal_gnss::GnssRuntimeState& state,
                         const std::optional<float> sigma)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kVerticalAccuracy);
  if (sigma.has_value())
  {
    universal_gnss::SetOptionalValue(
        state,
        universal_gnss::GnssCapability::kVerticalAccuracy,
        state.vertical_accuracy_m,
        *sigma);
  }
}

void SetCorrectionAge(universal_gnss::GnssRuntimeState& state,
                      const std::optional<float> correction_age_s)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kCorrectionAge);
  if (correction_age_s.has_value())
  {
    universal_gnss::SetOptionalValue(
        state,
        universal_gnss::GnssCapability::kCorrectionAge,
        state.correction_age_s,
        *correction_age_s);
  }
}

void SetTrackedAndUsedSatellites(universal_gnss::GnssRuntimeState& state,
                                 const std::optional<std::uint16_t> tracked,
                                 const std::optional<std::uint16_t> used)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesTracked);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesUsed);
  if (tracked.has_value())
  {
    universal_gnss::SetOptionalValue(
        state,
        universal_gnss::GnssCapability::kSatellitesTracked,
        state.satellites_tracked,
        *tracked);
  }
  if (used.has_value())
  {
    universal_gnss::SetOptionalValue(
        state,
        universal_gnss::GnssCapability::kSatellitesUsed,
        state.satellites_used,
        *used);
  }
}

std::optional<universal_gnss::GnssBaselineSolutionStatus> ToBaselineSolutionStatus(
    const UnicoreSolutionStatus status)
{
  switch (status)
  {
    case UnicoreSolutionStatus::kSolComputed:
      return universal_gnss::GnssBaselineSolutionStatus::kComputed;
    case UnicoreSolutionStatus::kInsufficientObs:
      return universal_gnss::GnssBaselineSolutionStatus::kInsufficientObservations;
    case UnicoreSolutionStatus::kNoConvergence:
      return universal_gnss::GnssBaselineSolutionStatus::kNoConvergence;
    case UnicoreSolutionStatus::kCovTrace:
      return universal_gnss::GnssBaselineSolutionStatus::kCovarianceTraceExceeded;
    case UnicoreSolutionStatus::kUnknown:
    default:
      return std::nullopt;
  }
}

std::optional<universal_gnss::GnssBaselineSolutionStatus> ToBaselineSolutionStatus(
    const UnicoreDualAntennaStatus status)
{
  switch (status)
  {
    case UnicoreDualAntennaStatus::kWithinLimit:
      return universal_gnss::GnssBaselineSolutionStatus::kComputed;
    case UnicoreDualAntennaStatus::kNotSolved:
      return universal_gnss::GnssBaselineSolutionStatus::kNotSolved;
    case UnicoreDualAntennaStatus::kOutOfLimit:
      return universal_gnss::GnssBaselineSolutionStatus::kOutOfTolerance;
    case UnicoreDualAntennaStatus::kNotConfigured:
      return universal_gnss::GnssBaselineSolutionStatus::kNotConfigured;
    case UnicoreDualAntennaStatus::kUnknown:
    default:
      return std::nullopt;
  }
}

bool BaselineStatusMeansSolved(const universal_gnss::GnssBaselineSolutionStatus status)
{
  return status == universal_gnss::GnssBaselineSolutionStatus::kComputed;
}

bool BaselineStatusMeansKnownFalse(const universal_gnss::GnssBaselineSolutionStatus status)
{
  switch (status)
  {
    case universal_gnss::GnssBaselineSolutionStatus::kNotSolved:
    case universal_gnss::GnssBaselineSolutionStatus::kInsufficientObservations:
    case universal_gnss::GnssBaselineSolutionStatus::kNoConvergence:
    case universal_gnss::GnssBaselineSolutionStatus::kOutOfTolerance:
    case universal_gnss::GnssBaselineSolutionStatus::kCovarianceTraceExceeded:
    case universal_gnss::GnssBaselineSolutionStatus::kNotConfigured:
      return true;
    case universal_gnss::GnssBaselineSolutionStatus::kUnknown:
    case universal_gnss::GnssBaselineSolutionStatus::kComputed:
    default:
      return false;
  }
}

void SetCanonicalBaselineStatus(
    universal_gnss::GnssRuntimeState& state,
    const std::optional<universal_gnss::GnssBaselineSolutionStatus> status)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kDualAntennaBaseline);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kBaselineSolutionStatus);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kDualAntennaHeading);

  if (!status.has_value())
  {
    return;
  }

  universal_gnss::SetOptionalValue(state,
                                   universal_gnss::GnssCapability::kBaselineSolutionStatus,
                                   state.baseline_solution_status,
                                   *status);
  if (BaselineStatusMeansSolved(*status))
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kDualAntennaBaseline,
                                     state.dual_antenna_baseline,
                                     true);
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kDualAntennaHeading,
                                     state.dual_antenna_heading,
                                     true);
    return;
  }

  if (BaselineStatusMeansKnownFalse(*status))
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kDualAntennaBaseline,
                                     state.dual_antenna_baseline,
                                     false);
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kDualAntennaHeading,
                                     state.dual_antenna_heading,
                                     false);
  }
}

void SetBaselineGeometryAndCompatibilityHeading(
    universal_gnss::GnssRuntimeState& state,
    const UnicoreSolutionStatus baseline_solution_status,
    const std::optional<float> baseline_length_m,
    const std::optional<float> baseline_azimuth_deg,
    const std::optional<float> baseline_pitch_deg)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHeading);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kBaselineAzimuth);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kBaselinePitch);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kBaselineLength);
  SetCanonicalBaselineStatus(state, ToBaselineSolutionStatus(baseline_solution_status));

  if (baseline_solution_status != UnicoreSolutionStatus::kSolComputed)
  {
    return;
  }

  if (baseline_length_m.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kBaselineLength,
                                     state.baseline_length_m,
                                     *baseline_length_m);
  }
  if (baseline_azimuth_deg.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kBaselineAzimuth,
                                     state.baseline_azimuth_deg,
                                     *baseline_azimuth_deg);
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kHeading,
                                     state.heading_deg,
                                     *baseline_azimuth_deg);
  }
  if (baseline_pitch_deg.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kBaselinePitch,
                                     state.baseline_pitch_deg,
                                     *baseline_pitch_deg);
  }
}

void SetCanonicalBaselineStatus(universal_gnss::GnssRuntimeState& state,
                                const UnicoreDualAntennaStatus status)
{
  SetCanonicalBaselineStatus(state, ToBaselineSolutionStatus(status));
}

void ApplyJammingState(universal_gnss::GnssRuntimeState& state,
                       const UnicoreJammingState status)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kInterferenceState);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kJammingState);

  switch (status)
  {
    case UnicoreJammingState::kNone:
      universal_gnss::SetOptionalValue(state,
                                       universal_gnss::GnssCapability::kInterferenceState,
                                       state.interference_detected,
                                       false);
      universal_gnss::SetOptionalValue(state,
                                       universal_gnss::GnssCapability::kJammingState,
                                       state.jamming_detected,
                                       false);
      break;
    case UnicoreJammingState::kJamming:
    case UnicoreJammingState::kStrongJamming:
      universal_gnss::SetOptionalValue(state,
                                       universal_gnss::GnssCapability::kInterferenceState,
                                       state.interference_detected,
                                       true);
      universal_gnss::SetOptionalValue(state,
                                       universal_gnss::GnssCapability::kJammingState,
                                       state.jamming_detected,
                                       true);
      break;
    case UnicoreJammingState::kUnknown:
    default:
      break;
  }
}

const char* DescribeJammingState(const UnicoreJammingState state)
{
  switch (state)
  {
    case UnicoreJammingState::kNone:
      return "no jamming";
    case UnicoreJammingState::kJamming:
      return "jamming detected";
    case UnicoreJammingState::kStrongJamming:
      return "strong jamming detected";
    case UnicoreJammingState::kUnknown:
    default:
      return "jamming state unknown";
  }
}

universal_gnss::GnssDiagnosticEvent BuildReceiverDiagnostic(
    const universal_gnss::GnssDiagnosticSeverity severity,
    const std::string& code,
    const std::string& message,
    const std::optional<ProtocolTimestampNs> timestamp_ns,
    const char* source)
{
  universal_gnss::GnssDiagnosticEvent event;
  event.severity = severity;
  event.category = universal_gnss::GnssDiagnosticCategory::kReceiver;
  event.code = code;
  event.message = message;
  event.timestamp_ns = timestamp_ns;
  event.source = std::string(source);
  return event;
}

}  // namespace

ParserResult<UnicorePvtslnRecord> ParseUnicorePvtsln(const UnicoreFrame& frame)
{
  const auto parsed = ParseAsciiHeader<16u>(frame, "PVTSLNA");
  if (!parsed.has_value())
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }

  std::array<std::string_view, 96u> fields{};
  std::size_t field_count = 0u;
  if (!TokenizeBody(parsed->body, fields, field_count) || field_count < 17u)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }

  UnicorePvtslnRecord record;
  record.header = parsed->header;
  record.best_position_type = ParsePositionType(fields[0]);
  if (!TryParseDouble(fields[1], record.best_altitude_m) ||
      !TryParseDouble(fields[2], record.best_latitude_deg) ||
      !TryParseDouble(fields[3], record.best_longitude_deg))
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }

  if (ParseOptionalFloat(fields[4], record.best_altitude_std_m) == OptionalFieldStatus::kInvalid ||
      ParseOptionalFloat(fields[5], record.best_latitude_std_m) == OptionalFieldStatus::kInvalid ||
      ParseOptionalFloat(fields[6], record.best_longitude_std_m) == OptionalFieldStatus::kInvalid ||
      ParseOptionalFloat(fields[7], record.best_diff_age_s) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }

  record.psr_position_type = ParsePositionType(fields[8]);
  if (ParseOptionalDouble(fields[9], record.psr_altitude_m) == OptionalFieldStatus::kInvalid ||
      ParseOptionalDouble(fields[10], record.psr_latitude_deg) == OptionalFieldStatus::kInvalid ||
      ParseOptionalDouble(fields[11], record.psr_longitude_deg) == OptionalFieldStatus::kInvalid ||
      ParseOptionalUnsigned16(fields[13], record.best_tracked_satellites) ==
          OptionalFieldStatus::kInvalid ||
      ParseOptionalUnsigned16(fields[14], record.best_used_satellites) ==
          OptionalFieldStatus::kInvalid ||
      ParseOptionalUnsigned16(fields[15], record.psr_tracked_satellites) ==
          OptionalFieldStatus::kInvalid ||
      ParseOptionalUnsigned16(fields[16], record.psr_used_satellites) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }

  if (field_count > 20u)
  {
    record.baseline_solution_status = ParseSolutionStatus(fields[20]);
  }
  if (field_count > 21u &&
      ParseOptionalFloat(fields[21], record.baseline_length_m) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 22u &&
      ParseOptionalFloat(fields[22], record.baseline_azimuth_deg) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 23u &&
      ParseOptionalFloat(fields[23], record.baseline_pitch_deg) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 24u &&
      ParseOptionalUnsigned16(fields[24], record.baseline_tracked_satellites) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 25u &&
      ParseOptionalUnsigned16(fields[25], record.baseline_used_satellites) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 28u && ParseOptionalFloat(fields[28], record.gdop) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 29u && ParseOptionalFloat(fields[29], record.pdop) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 30u && ParseOptionalFloat(fields[30], record.hdop) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 31u && ParseOptionalFloat(fields[31], record.htdop) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 32u && ParseOptionalFloat(fields[32], record.tdop) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }

  return ParserResult<UnicorePvtslnRecord>::RecordReady(record);
}

ParserResult<UnicoreBestNavRecord> ParseUnicoreBestNav(const UnicoreFrame& frame)
{
  const auto parsed = ParseAsciiHeader<16u>(frame, "BESTNAVA");
  if (!parsed.has_value())
  {
    return InvalidResult<UnicoreBestNavRecord>();
  }

  std::array<std::string_view, 48u> fields{};
  std::size_t field_count = 0u;
  if (!TokenizeBody(parsed->body, fields, field_count) || field_count < 16u)
  {
    return InvalidResult<UnicoreBestNavRecord>();
  }

  UnicoreBestNavRecord record;
  record.header = parsed->header;
  record.solution_status = ParseSolutionStatus(fields[0]);
  record.position_type = ParsePositionType(fields[1]);
  if (!TryParseDouble(fields[2], record.latitude_deg) ||
      !TryParseDouble(fields[3], record.longitude_deg) ||
      !TryParseDouble(fields[4], record.altitude_m))
  {
    return InvalidResult<UnicoreBestNavRecord>();
  }

  if (ParseOptionalFloat(fields[5], record.undulation_m) == OptionalFieldStatus::kInvalid ||
      ParseOptionalBoolEquals(fields[6], "WGS84", record.datum_is_wgs84) ==
          OptionalFieldStatus::kInvalid ||
      ParseOptionalFloat(fields[7], record.latitude_std_m) == OptionalFieldStatus::kInvalid ||
      ParseOptionalFloat(fields[8], record.longitude_std_m) == OptionalFieldStatus::kInvalid ||
      ParseOptionalFloat(fields[9], record.altitude_std_m) == OptionalFieldStatus::kInvalid ||
      ParseOptionalFloat(fields[11], record.diff_age_s) == OptionalFieldStatus::kInvalid)
   {
     return InvalidResult<UnicoreBestNavRecord>();
   }

  const bool has_shifted_um98x_satellite_tail =
      field_count > 21u &&
      LooksUnsignedIntegerField(fields[12]) &&
      LooksUnsignedIntegerField(fields[13]) &&
      ParseSolutionStatus(fields[20]) != UnicoreSolutionStatus::kUnknown &&
      ParsePositionType(fields[21]) != UnicorePositionType::kUnknown;

  if (has_shifted_um98x_satellite_tail)
  {
    record.solution_age_s.reset();
    if (ParseOptionalUnsigned16(fields[12], record.tracked_satellites) ==
            OptionalFieldStatus::kInvalid ||
        ParseOptionalUnsigned16(fields[13], record.used_satellites) ==
            OptionalFieldStatus::kInvalid)
    {
      return InvalidResult<UnicoreBestNavRecord>();
    }
  }
  else
  {
    if (ParseOptionalFloat(fields[12], record.solution_age_s) ==
            OptionalFieldStatus::kInvalid ||
        ParseOptionalUnsigned16(fields[13], record.tracked_satellites) ==
            OptionalFieldStatus::kInvalid ||
        ParseOptionalUnsigned16(fields[14], record.used_satellites) ==
            OptionalFieldStatus::kInvalid)
    {
      return InvalidResult<UnicoreBestNavRecord>();
    }
  }

  return ParserResult<UnicoreBestNavRecord>::RecordReady(record);
}

ParserResult<UnicoreRtkStatusRecord> ParseUnicoreRtkStatus(const UnicoreFrame& frame)
{
  const auto parsed = ParseAsciiHeader<16u>(frame, "RTKSTATUSA");
  if (!parsed.has_value())
  {
    return InvalidResult<UnicoreRtkStatusRecord>();
  }

  std::array<std::string_view, 24u> fields{};
  std::size_t field_count = 0u;
  if (!TokenizeBody(parsed->body, fields, field_count) || field_count < 17u)
  {
    return InvalidResult<UnicoreRtkStatusRecord>();
  }

  UnicoreRtkStatusRecord record;
  record.header = parsed->header;
  record.position_type = ParsePositionType(fields[11]);
  record.dual_antenna_status = ParseDualAntennaStatus(fields[14]);
  if (ParseOptionalUnsigned32(fields[12], record.calculation_status) ==
          OptionalFieldStatus::kInvalid ||
      ParseOptionalUnsigned8(fields[13], record.ionosphere_effect) ==
          OptionalFieldStatus::kInvalid ||
      ParseOptionalUnsigned16(fields[15], record.adr_observation_count) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicoreRtkStatusRecord>();
  }

  return ParserResult<UnicoreRtkStatusRecord>::RecordReady(record);
}

ParserResult<UnicoreRtcmStatusRecord> ParseUnicoreRtcmStatus(const UnicoreFrame& frame)
{
  const auto parsed = ParseAsciiHeader<16u>(frame, "RTCMSTATUSA");
  if (!parsed.has_value())
  {
    return InvalidResult<UnicoreRtcmStatusRecord>();
  }

  std::array<std::string_view, 16u> fields{};
  std::size_t field_count = 0u;
  if (!TokenizeBody(parsed->body, fields, field_count) || field_count < 10u)
  {
    return InvalidResult<UnicoreRtcmStatusRecord>();
  }

  unsigned int message_type = 0u;
  unsigned int message_count = 0u;
  unsigned int base_station_id = 0u;
  unsigned int satellites_in_message = 0u;
  unsigned int l1 = 0u;
  unsigned int l2 = 0u;
  unsigned int l3 = 0u;
  unsigned int l4 = 0u;
  unsigned int l5 = 0u;
  unsigned int l6 = 0u;
  if (!TryParseUnsigned(fields[0], message_type) ||
      !TryParseUnsigned(fields[1], message_count) ||
      !TryParseUnsigned(fields[2], base_station_id) ||
      !TryParseUnsigned(fields[3], satellites_in_message) ||
      !TryParseUnsigned(fields[4], l1) ||
      !TryParseUnsigned(fields[5], l2) ||
      !TryParseUnsigned(fields[6], l3) ||
      !TryParseUnsigned(fields[7], l4) ||
      !TryParseUnsigned(fields[8], l5) ||
      !TryParseUnsigned(fields[9], l6) ||
      l1 > std::numeric_limits<std::uint8_t>::max() ||
      l2 > std::numeric_limits<std::uint8_t>::max() ||
      l3 > std::numeric_limits<std::uint8_t>::max() ||
      l4 > std::numeric_limits<std::uint8_t>::max() ||
      l5 > std::numeric_limits<std::uint8_t>::max() ||
      l6 > std::numeric_limits<std::uint8_t>::max())
  {
    return InvalidResult<UnicoreRtcmStatusRecord>();
  }

  UnicoreRtcmStatusRecord record;
  record.header = parsed->header;
  record.message_type = static_cast<std::uint32_t>(message_type);
  record.message_count = static_cast<std::uint32_t>(message_count);
  record.base_station_id = static_cast<std::uint32_t>(base_station_id);
  record.satellites_in_message = static_cast<std::uint32_t>(satellites_in_message);
  record.l1_observables = static_cast<std::uint8_t>(l1);
  record.l2_observables = static_cast<std::uint8_t>(l2);
  record.l3_observables = static_cast<std::uint8_t>(l3);
  record.l4_observables = static_cast<std::uint8_t>(l4);
  record.l5_observables = static_cast<std::uint8_t>(l5);
  record.l6_observables = static_cast<std::uint8_t>(l6);
  return ParserResult<UnicoreRtcmStatusRecord>::RecordReady(record);
}

ParserResult<UnicoreBestSatRecord> ParseUnicoreBestSat(const UnicoreFrame& frame)
{
  const auto parsed = ParseAsciiHeader<16u>(frame, "BESTSATA");
  if (!parsed.has_value())
  {
    return InvalidResult<UnicoreBestSatRecord>();
  }

  std::string_view remaining = parsed->body;
  std::string_view field{};
  unsigned int entry_count = 0u;
  if (!ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, entry_count) ||
      entry_count > kMaxUnicoreBestSatEntries)
  {
    return InvalidResult<UnicoreBestSatRecord>();
  }

  UnicoreBestSatRecord record;
  record.header = parsed->header;
  record.entry_count = static_cast<std::uint16_t>(entry_count);

  for (unsigned int satellite_index = 0u; satellite_index < entry_count; ++satellite_index)
  {
    UnicoreBestSatSatellite satellite;

    if (!ConsumeCsvField(remaining, field))
    {
      return InvalidResult<UnicoreBestSatRecord>();
    }
    satellite.constellation = ParseBestSatConstellation(field);

    if (!ConsumeCsvField(remaining, field) ||
        !ParseBestSatSatelliteId(
            field, satellite.satellite_id, satellite.glonass_frequency_channel))
    {
      return InvalidResult<UnicoreBestSatRecord>();
    }

    if (!ConsumeCsvField(remaining, field))
    {
      return InvalidResult<UnicoreBestSatRecord>();
    }
    const std::string_view status_text = TrimField(field);
    if (status_text.empty())
    {
      return InvalidResult<UnicoreBestSatRecord>();
    }
    satellite.status_good = (status_text == "GOOD");

    unsigned int signal_mask = 0u;
    if (!ConsumeCsvField(remaining, field) || !TryParseUnsignedBase(field, 16, signal_mask))
    {
      return InvalidResult<UnicoreBestSatRecord>();
    }

    satellite.signal_mask = static_cast<std::uint32_t>(signal_mask);
    satellite.used_in_solution =
        (satellite.signal_mask & BestSatUsedSignalMask(satellite.constellation)) != 0u;
    satellite.common_view = (satellite.signal_mask & 0x10u) != 0u;

    record.satellites[record.parsed_satellite_count++] = satellite;
  }

  if (!remaining.empty())
  {
    return InvalidResult<UnicoreBestSatRecord>();
  }

  return ParserResult<UnicoreBestSatRecord>::RecordReady(record);
}

ParserResult<UnicoreSatsInfoRecord> ParseUnicoreSatsInfo(const UnicoreFrame& frame)
{
  const auto parsed = ParseAsciiHeader<16u>(frame, "SATSINFOA");
  if (!parsed.has_value())
  {
    return InvalidResult<UnicoreSatsInfoRecord>();
  }

  std::string_view remaining = parsed->body;
  std::string_view field{};
  unsigned int tracked_satellite_count = 0u;
  unsigned int version = 0u;
  unsigned int reserve0 = 0u;
  unsigned int reserve1 = 0u;
  unsigned int reserve2 = 0u;
  unsigned int frequency_flag = 0u;
  if (!ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, tracked_satellite_count) ||
      !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, version) ||
      !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, reserve0) ||
      !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, reserve1) ||
      !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, reserve2) ||
      !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, frequency_flag) ||
      tracked_satellite_count > kMaxUnicoreSatsInfoSatellites ||
      version > std::numeric_limits<std::uint8_t>::max() ||
      reserve0 > std::numeric_limits<std::uint8_t>::max() ||
      reserve1 > std::numeric_limits<std::uint8_t>::max() ||
      reserve2 > std::numeric_limits<std::uint8_t>::max() ||
      frequency_flag > std::numeric_limits<std::uint8_t>::max())
  {
    return InvalidResult<UnicoreSatsInfoRecord>();
  }

  UnicoreSatsInfoRecord record;
  record.header = parsed->header;
  record.tracked_satellite_count = static_cast<std::uint16_t>(tracked_satellite_count);
  record.version = static_cast<std::uint8_t>(version);
  record.frequency_flag = static_cast<std::uint8_t>(frequency_flag);

  for (unsigned int satellite_index = 0u; satellite_index < tracked_satellite_count; ++satellite_index)
  {
    unsigned int satellite_id = 0u;
    int azimuth_deg = 0;
    int elevation_deg = 0;
    unsigned int system_id = 0u;
    unsigned int snr = 0u;
    unsigned int frequency_status = 0u;
    unsigned int frequency_count = 0u;
    if (!ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, satellite_id) ||
        !ConsumeCsvField(remaining, field) || !TryParseSigned(field, azimuth_deg) ||
        !ConsumeCsvField(remaining, field) || !TryParseSigned(field, elevation_deg) ||
        !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, system_id) ||
        !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, snr) ||
        !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, frequency_status) ||
        !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, frequency_count) ||
        satellite_id > std::numeric_limits<std::uint16_t>::max() ||
        azimuth_deg < std::numeric_limits<std::int16_t>::min() ||
        azimuth_deg > std::numeric_limits<std::int16_t>::max() ||
        elevation_deg < std::numeric_limits<std::int16_t>::min() ||
        elevation_deg > std::numeric_limits<std::int16_t>::max() ||
        system_id > std::numeric_limits<std::uint8_t>::max() ||
        snr > std::numeric_limits<std::uint8_t>::max() ||
        frequency_status > std::numeric_limits<std::uint8_t>::max() ||
        frequency_count == 0u ||
        frequency_count > std::numeric_limits<std::uint8_t>::max())
    {
      return InvalidResult<UnicoreSatsInfoRecord>();
    }

    UnicoreSatsInfoSatellite satellite;
    satellite.satellite_id = static_cast<std::uint16_t>(satellite_id);
    satellite.azimuth_deg = static_cast<std::int16_t>(azimuth_deg);
    satellite.elevation_deg = static_cast<std::int16_t>(elevation_deg);
    satellite.system_id = static_cast<std::uint8_t>(system_id);
    satellite.frequency_status = static_cast<std::uint8_t>(frequency_status);
    satellite.frequency_count = static_cast<std::uint8_t>(frequency_count);
    satellite.cn0_db_hz = static_cast<std::uint8_t>(snr);

    for (unsigned int frequency_index = 1u; frequency_index < frequency_count; ++frequency_index)
    {
      unsigned int next_system_id = 0u;
      unsigned int next_snr = 0u;
      unsigned int next_frequency_status = 0u;
      unsigned int next_frequency_count = 0u;
      if (!ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, next_system_id) ||
          !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, next_snr) ||
          !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, next_frequency_status) ||
          !ConsumeCsvField(remaining, field) || !TryParseUnsigned(field, next_frequency_count) ||
          next_system_id > std::numeric_limits<std::uint8_t>::max() ||
          next_snr > std::numeric_limits<std::uint8_t>::max() ||
          next_frequency_status > std::numeric_limits<std::uint8_t>::max() ||
          next_frequency_count == 0u ||
          next_frequency_count > std::numeric_limits<std::uint8_t>::max())
      {
        return InvalidResult<UnicoreSatsInfoRecord>();
      }

      satellite.cn0_db_hz = std::max(satellite.cn0_db_hz, static_cast<std::uint8_t>(next_snr));
    }

    record.satellites[record.parsed_satellite_count++] = satellite;
  }

  if (!remaining.empty())
  {
    return InvalidResult<UnicoreSatsInfoRecord>();
  }

  return ParserResult<UnicoreSatsInfoRecord>::RecordReady(record);
}

ParserResult<UnicoreJamStatusRecord> ParseUnicoreJamStatus(const UnicoreFrame& frame)
{
  const auto parsed = ParseAsciiHeader<16u>(frame, "JAMSTATUSA");
  if (!parsed.has_value())
  {
    return InvalidResult<UnicoreJamStatusRecord>();
  }

  std::array<std::string_view, 8u> fields{};
  std::size_t field_count = 0u;
  if (!TokenizeBody(parsed->body, fields, field_count) || field_count < 5u)
  {
    return InvalidResult<UnicoreJamStatusRecord>();
  }

  unsigned int cw_ratio = 0u;
  unsigned int cw_flag = 0u;
  if (!TryParseUnsigned(fields[1], cw_ratio) || !TryParseUnsigned(fields[2], cw_flag) ||
      cw_ratio > std::numeric_limits<std::uint8_t>::max())
  {
    return InvalidResult<UnicoreJamStatusRecord>();
  }

  UnicoreJamStatusRecord record;
  record.header = parsed->header;
  record.position_type = ParsePositionType(fields[0]);
  record.cw_ratio = static_cast<std::uint8_t>(cw_ratio);
  record.cw_state = ParseJammingState(cw_flag);
  return ParserResult<UnicoreJamStatusRecord>::RecordReady(record);
}

ParserResult<UnicoreFreqJamStatusRecord> ParseUnicoreFreqJamStatus(const UnicoreFrame& frame)
{
  const auto parsed = ParseAsciiHeader<16u>(frame, "FREQJAMSTATUSA");
  if (!parsed.has_value())
  {
    return InvalidResult<UnicoreFreqJamStatusRecord>();
  }

  std::array<std::string_view, 12u> fields{};
  std::size_t field_count = 0u;
  if (!TokenizeBody(parsed->body, fields, field_count) || field_count < 9u)
  {
    return InvalidResult<UnicoreFreqJamStatusRecord>();
  }

  unsigned int l1_ratio = 0u;
  unsigned int l1_flag = 0u;
  unsigned int l2_ratio = 0u;
  unsigned int l2_flag = 0u;
  unsigned int l5_ratio = 0u;
  unsigned int l5_flag = 0u;
  if (!TryParseUnsigned(fields[1], l1_ratio) || !TryParseUnsigned(fields[2], l1_flag) ||
      !TryParseUnsigned(fields[3], l2_ratio) || !TryParseUnsigned(fields[4], l2_flag) ||
      !TryParseUnsigned(fields[5], l5_ratio) || !TryParseUnsigned(fields[6], l5_flag) ||
      l1_ratio > std::numeric_limits<std::uint8_t>::max() ||
      l2_ratio > std::numeric_limits<std::uint8_t>::max() ||
      l5_ratio > std::numeric_limits<std::uint8_t>::max())
  {
    return InvalidResult<UnicoreFreqJamStatusRecord>();
  }

  UnicoreFreqJamStatusRecord record;
  record.header = parsed->header;
  record.position_type = ParsePositionType(fields[0]);
  record.l1.cw_ratio = static_cast<std::uint8_t>(l1_ratio);
  record.l1.cw_state = ParseJammingState(l1_flag);
  record.l2.cw_ratio = static_cast<std::uint8_t>(l2_ratio);
  record.l2.cw_state = ParseJammingState(l2_flag);
  record.l5.cw_ratio = static_cast<std::uint8_t>(l5_ratio);
  record.l5.cw_state = ParseJammingState(l5_flag);
  return ParserResult<UnicoreFreqJamStatusRecord>::RecordReady(record);
}

ParserResult<UnicoreHwStatusRecord> ParseUnicoreHwStatus(const UnicoreFrame& frame)
{
  const auto parsed = ParseAsciiHeader<16u>(frame, "HWSTATUSA");
  if (!parsed.has_value())
  {
    return InvalidResult<UnicoreHwStatusRecord>();
  }

  std::array<std::string_view, 16u> fields{};
  std::size_t field_count = 0u;
  if (!TokenizeBody(parsed->body, fields, field_count) || field_count < 12u)
  {
    return InvalidResult<UnicoreHwStatusRecord>();
  }

  int reserved_counter = 0;
  double dc09_v = 0.0;
  double dc10_v = 0.0;
  double dc18_v = 0.0;
  unsigned int clock_flag = 0u;
  double clock_drift_mps = 0.0;
  unsigned int hw_flag = 0u;
  unsigned int pll_lock = 0u;
  if (!TryParseSigned(fields[0], reserved_counter) ||
      !TryParseDouble(fields[1], dc09_v) ||
      !TryParseDouble(fields[2], dc10_v) ||
      !TryParseDouble(fields[3], dc18_v) ||
      !TryParseUnsigned(fields[4], clock_flag) ||
      !TryParseDouble(fields[5], clock_drift_mps) ||
      !TryParseUnsignedAutoBase(fields[7], hw_flag) ||
      !TryParseUnsignedAutoBase(fields[9], pll_lock) ||
      clock_flag > 1u ||
      hw_flag > std::numeric_limits<std::uint8_t>::max() ||
      pll_lock > std::numeric_limits<std::uint16_t>::max())
  {
    return InvalidResult<UnicoreHwStatusRecord>();
  }

  UnicoreHwStatusRecord record;
  record.header = parsed->header;
  record.reserved_counter = static_cast<std::int32_t>(reserved_counter);
  record.dc09_v = static_cast<float>(dc09_v);
  record.dc10_v = static_cast<float>(dc10_v);
  record.dc18_v = static_cast<float>(dc18_v);
  record.clock_drift_valid = (clock_flag == 1u);
  record.clock_drift_mps = static_cast<float>(clock_drift_mps);
  record.hw_flag = static_cast<std::uint8_t>(hw_flag);
  record.pll_lock = static_cast<std::uint16_t>(pll_lock);
  return ParserResult<UnicoreHwStatusRecord>::RecordReady(record);
}

ParserResult<UnicoreAgcRecord> ParseUnicoreAgc(const UnicoreFrame& frame)
{
  const auto parsed = ParseAsciiHeader<16u>(frame, "AGCA");
  if (!parsed.has_value())
  {
    return InvalidResult<UnicoreAgcRecord>();
  }

  std::array<std::string_view, 12u> fields{};
  std::size_t field_count = 0u;
  if (!TokenizeBody(parsed->body, fields, field_count) || field_count < 10u)
  {
    return InvalidResult<UnicoreAgcRecord>();
  }

  UnicoreAgcRecord record;
  record.header = parsed->header;
  if (ParseOptionalAgcRegister(fields[0], record.ant1_l1) == OptionalFieldStatus::kInvalid ||
      ParseOptionalAgcRegister(fields[1], record.ant1_l2) == OptionalFieldStatus::kInvalid ||
      ParseOptionalAgcRegister(fields[2], record.ant1_l5) == OptionalFieldStatus::kInvalid ||
      ParseOptionalAgcRegister(fields[5], record.ant2_l1) == OptionalFieldStatus::kInvalid ||
      ParseOptionalAgcRegister(fields[6], record.ant2_l2) == OptionalFieldStatus::kInvalid ||
      ParseOptionalAgcRegister(fields[7], record.ant2_l5) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicoreAgcRecord>();
  }

  return ParserResult<UnicoreAgcRecord>::RecordReady(record);
}

ParserResult<UnicoreBestNavBRecord> ParseUnicoreBestNavB(const UnicoreBinaryFrame& frame)
{
  constexpr std::uint16_t kBestNavBMessageId = 2118u;
  constexpr std::size_t kBestNavBPayloadLength = 120u;
  constexpr std::uint32_t kWgs84DatumId = 61u;

  if (frame.protocol != ProtocolType::kUnicore ||
      frame.message_id != kBestNavBMessageId ||
      frame.checksum_status != ChecksumStatus::kValid ||
      frame.payload.size() != kBestNavBPayloadLength)
  {
    return InvalidResult<UnicoreBestNavBRecord>();
  }

  UnicoreBestNavBRecord record;
  record.header.timestamp_ns = frame.timestamp_ns;
  record.header.cpu_idle_percent = frame.cpu_idle;
  record.header.message_id = frame.message_id;
  record.header.payload_length = frame.payload_length;
  record.header.time_reference_raw = frame.time_ref;
  record.header.time_status_raw = frame.time_status;
  record.header.gps_week = frame.week_number;
  record.header.gps_millis_of_week = frame.milliseconds_of_week;
  record.header.format_version = frame.header_version;
  record.header.reserved = frame.reserved;
  record.header.leap_seconds = frame.leap_seconds;
  record.header.output_delay_ms = frame.delay_ms;

  record.solution_status = ParseBinarySolutionStatus(ReadLittleEndian32(frame.payload.data() + 0u));
  record.position_type = ParseBinaryPositionType(ReadLittleEndian32(frame.payload.data() + 4u));
  record.latitude_deg = ReadLittleEndianFloat64(frame.payload.data() + 8u);
  record.longitude_deg = ReadLittleEndianFloat64(frame.payload.data() + 16u);
  record.altitude_m = ReadLittleEndianFloat64(frame.payload.data() + 24u);
  record.undulation_m = ReadLittleEndianFloat32(frame.payload.data() + 32u);
  record.datum_is_wgs84 = (ReadLittleEndian32(frame.payload.data() + 36u) == kWgs84DatumId);
  record.latitude_std_m = ReadLittleEndianFloat32(frame.payload.data() + 40u);
  record.longitude_std_m = ReadLittleEndianFloat32(frame.payload.data() + 44u);
  record.altitude_std_m = ReadLittleEndianFloat32(frame.payload.data() + 48u);
  record.diff_age_s = ReadLittleEndianFloat32(frame.payload.data() + 56u);
  record.solution_age_s = ReadLittleEndianFloat32(frame.payload.data() + 60u);
  record.tracked_satellites = static_cast<std::uint16_t>(frame.payload[64u]);
  record.used_satellites = static_cast<std::uint16_t>(frame.payload[65u]);
  return ParserResult<UnicoreBestNavBRecord>::RecordReady(record);
}

ParserResult<UnicorePvtslnBRecord> ParseUnicorePvtslnB(const UnicoreBinaryFrame& frame)
{
  constexpr std::uint16_t kPvtslnBMessageId = 1021u;
  constexpr std::size_t kPvtslnBPayloadLength = 224u;

  if (frame.protocol != ProtocolType::kUnicore ||
      frame.message_id != kPvtslnBMessageId ||
      frame.checksum_status != ChecksumStatus::kValid ||
      frame.payload.size() != kPvtslnBPayloadLength)
  {
    return InvalidResult<UnicorePvtslnBRecord>();
  }

  UnicorePvtslnBRecord record;
  record.header.timestamp_ns = frame.timestamp_ns;
  record.header.cpu_idle_percent = frame.cpu_idle;
  record.header.message_id = frame.message_id;
  record.header.payload_length = frame.payload_length;
  record.header.time_reference_raw = frame.time_ref;
  record.header.time_status_raw = frame.time_status;
  record.header.gps_week = frame.week_number;
  record.header.gps_millis_of_week = frame.milliseconds_of_week;
  record.header.format_version = frame.header_version;
  record.header.reserved = frame.reserved;
  record.header.leap_seconds = frame.leap_seconds;
  record.header.output_delay_ms = frame.delay_ms;

  record.best_position_type = ParseBinaryPositionType(ReadLittleEndian32(frame.payload.data() + 0u));
  record.best_altitude_m = static_cast<double>(ReadLittleEndianFloat32(frame.payload.data() + 4u));
  record.best_latitude_deg = ReadLittleEndianFloat64(frame.payload.data() + 8u);
  record.best_longitude_deg = ReadLittleEndianFloat64(frame.payload.data() + 16u);
  record.best_altitude_std_m = ReadLittleEndianFloat32(frame.payload.data() + 24u);
  record.best_latitude_std_m = ReadLittleEndianFloat32(frame.payload.data() + 28u);
  record.best_longitude_std_m = ReadLittleEndianFloat32(frame.payload.data() + 32u);
  record.best_diff_age_s = ReadLittleEndianFloat32(frame.payload.data() + 36u);

  record.psr_position_type = ParseBinaryPositionType(ReadLittleEndian32(frame.payload.data() + 40u));
  record.psr_altitude_m = static_cast<double>(ReadLittleEndianFloat32(frame.payload.data() + 44u));
  record.psr_latitude_deg = ReadLittleEndianFloat64(frame.payload.data() + 48u);
  record.psr_longitude_deg = ReadLittleEndianFloat64(frame.payload.data() + 56u);

  record.undulation_m = ReadLittleEndianFloat32(frame.payload.data() + 64u);
  record.best_tracked_satellites = static_cast<std::uint16_t>(frame.payload[68u]);
  record.best_used_satellites = static_cast<std::uint16_t>(frame.payload[69u]);
  record.psr_tracked_satellites = static_cast<std::uint16_t>(frame.payload[70u]);
  record.psr_used_satellites = static_cast<std::uint16_t>(frame.payload[71u]);

  record.baseline_solution_status =
      ParseBinarySolutionStatus(ReadLittleEndian32(frame.payload.data() + 96u));
  record.baseline_length_m = ReadLittleEndianFloat32(frame.payload.data() + 100u);
  record.baseline_azimuth_deg = ReadLittleEndianFloat32(frame.payload.data() + 104u);
  record.baseline_pitch_deg = ReadLittleEndianFloat32(frame.payload.data() + 108u);
  record.baseline_tracked_satellites = static_cast<std::uint16_t>(frame.payload[112u]);
  record.baseline_used_satellites = static_cast<std::uint16_t>(frame.payload[113u]);

  record.gdop = ReadLittleEndianFloat32(frame.payload.data() + 116u);
  record.pdop = ReadLittleEndianFloat32(frame.payload.data() + 120u);
  record.hdop = ReadLittleEndianFloat32(frame.payload.data() + 124u);
  record.htdop = ReadLittleEndianFloat32(frame.payload.data() + 128u);
  record.tdop = ReadLittleEndianFloat32(frame.payload.data() + 132u);
  return ParserResult<UnicorePvtslnBRecord>::RecordReady(record);
}

universal_gnss::GnssRuntimeState UnicorePvtslnToRuntimeState(const UnicorePvtslnRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;

  ApplyFixType(state, record.best_position_type);
  ApplyRtkMode(state, record.best_position_type);
  ApplyCorrectionState(state, record.best_position_type);
  if (state.fix_valid)
  {
    state.latitude_deg = record.best_latitude_deg;
    state.longitude_deg = record.best_longitude_deg;
    state.altitude_m = record.best_altitude_m;
  }

  SetHorizontalAccuracyFromSigmas(
      state, record.best_latitude_std_m, record.best_longitude_std_m);
  SetVerticalAccuracy(state, record.best_altitude_std_m);
  SetTrackedAndUsedSatellites(
      state, record.best_tracked_satellites, record.best_used_satellites);
  SetCorrectionAge(state, record.best_diff_age_s);

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHdop);
  if (record.hdop.has_value())
  {
    universal_gnss::SetOptionalValue(
        state,
        universal_gnss::GnssCapability::kHdop,
        state.hdop,
        *record.hdop);
  }

  SetBaselineGeometryAndCompatibilityHeading(state,
                                             record.baseline_solution_status,
                                             record.baseline_length_m,
                                             record.baseline_azimuth_deg,
                                             record.baseline_pitch_deg);
  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssRuntimeState UnicoreBestNavToRuntimeState(const UnicoreBestNavRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;

  ApplyFixType(state, record.position_type);
  if (record.solution_status != UnicoreSolutionStatus::kSolComputed)
  {
    state.fix_valid = false;
    if (state.fix_type != universal_gnss::GnssFixType::kUnknown)
    {
      state.fix_type = universal_gnss::GnssFixType::kNoFix;
    }
  }
  ApplyRtkMode(state, record.position_type);
  ApplyCorrectionState(state, record.position_type);
  if (state.fix_valid)
  {
    state.latitude_deg = record.latitude_deg;
    state.longitude_deg = record.longitude_deg;
    state.altitude_m = record.altitude_m;
  }

  SetHorizontalAccuracyFromSigmas(
      state, record.latitude_std_m, record.longitude_std_m);
  SetVerticalAccuracy(state, record.altitude_std_m);
  SetTrackedAndUsedSatellites(
      state, record.tracked_satellites, record.used_satellites);
  SetCorrectionAge(state, record.diff_age_s);

  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssRuntimeState UnicoreBestNavBToRuntimeState(
    const UnicoreBestNavBRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;

  ApplyFixType(state, record.position_type);
  if (record.solution_status != UnicoreSolutionStatus::kSolComputed)
  {
    state.fix_valid = false;
    if (state.fix_type != universal_gnss::GnssFixType::kUnknown)
    {
      state.fix_type = universal_gnss::GnssFixType::kNoFix;
    }
  }
  ApplyRtkMode(state, record.position_type);
  ApplyCorrectionState(state, record.position_type);
  if (state.fix_valid)
  {
    state.latitude_deg = record.latitude_deg;
    state.longitude_deg = record.longitude_deg;
    state.altitude_m = record.altitude_m;
  }

  SetHorizontalAccuracyFromSigmas(state, record.latitude_std_m, record.longitude_std_m);
  SetVerticalAccuracy(state, record.altitude_std_m);
  SetTrackedAndUsedSatellites(state, record.tracked_satellites, record.used_satellites);
  SetCorrectionAge(state, record.diff_age_s);

  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssRuntimeState UnicorePvtslnBToRuntimeState(
    const UnicorePvtslnBRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;

  ApplyFixType(state, record.best_position_type);
  ApplyRtkMode(state, record.best_position_type);
  ApplyCorrectionState(state, record.best_position_type);
  if (state.fix_valid)
  {
    state.latitude_deg = record.best_latitude_deg;
    state.longitude_deg = record.best_longitude_deg;
    state.altitude_m = record.best_altitude_m;
  }

  SetHorizontalAccuracyFromSigmas(
      state, record.best_latitude_std_m, record.best_longitude_std_m);
  SetVerticalAccuracy(state, record.best_altitude_std_m);
  SetTrackedAndUsedSatellites(
      state, record.best_tracked_satellites, record.best_used_satellites);
  SetCorrectionAge(state, record.best_diff_age_s);

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHdop);
  if (record.hdop.has_value())
  {
    universal_gnss::SetOptionalValue(
        state,
        universal_gnss::GnssCapability::kHdop,
        state.hdop,
        *record.hdop);
  }

  SetBaselineGeometryAndCompatibilityHeading(state,
                                             record.baseline_solution_status,
                                             record.baseline_length_m,
                                             record.baseline_azimuth_deg,
                                             record.baseline_pitch_deg);
  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssRuntimeState UnicoreRtkStatusToRuntimeState(
    const UnicoreRtkStatusRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;

  ApplyFixType(state, record.position_type);
  ApplyRtkMode(state, record.position_type);
  ApplyCorrectionState(state, record.position_type);
  SetCanonicalBaselineStatus(state, record.dual_antenna_status);

  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssRuntimeState UnicoreRtcmStatusToRuntimeState(
    const UnicoreRtcmStatusRecord& record)
{
  (void)record;
  universal_gnss::GnssRuntimeState state;
  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssRuntimeState UnicoreBestSatToRuntimeState(const UnicoreBestSatRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;

  std::uint16_t used_satellite_count = 0u;
  for (std::size_t satellite_index = 0u;
       satellite_index < static_cast<std::size_t>(record.parsed_satellite_count);
       ++satellite_index)
  {
    if (record.satellites[satellite_index].used_in_solution)
    {
      ++used_satellite_count;
    }
  }

  SetTrackedAndUsedSatellites(state, record.entry_count, used_satellite_count);
  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssRuntimeState UnicoreSatsInfoToRuntimeState(const UnicoreSatsInfoRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesTracked);
  universal_gnss::SetOptionalValue(
      state,
      universal_gnss::GnssCapability::kSatellitesTracked,
      state.satellites_tracked,
      record.tracked_satellite_count);

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kMeanCn0);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kMaxCn0);

  float cn0_sum = 0.0f;
  std::size_t cn0_count = 0u;
  float max_cn0 = 0.0f;
  for (std::size_t satellite_index = 0u;
       satellite_index < static_cast<std::size_t>(record.parsed_satellite_count);
       ++satellite_index)
  {
    const float cn0 = static_cast<float>(record.satellites[satellite_index].cn0_db_hz);
    if (cn0 <= 0.0f)
    {
      continue;
    }

    cn0_sum += cn0;
    max_cn0 = std::max(max_cn0, cn0);
    ++cn0_count;
  }

  if (cn0_count > 0u)
  {
    universal_gnss::SetOptionalValue(
        state,
        universal_gnss::GnssCapability::kMeanCn0,
        state.mean_cn0_db_hz,
        cn0_sum / static_cast<float>(cn0_count));
    universal_gnss::SetOptionalValue(
        state,
        universal_gnss::GnssCapability::kMaxCn0,
        state.max_cn0_db_hz,
        max_cn0);
  }

  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssRuntimeState UnicoreJamStatusToRuntimeState(
    const UnicoreJamStatusRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;
  ApplyJammingState(state, record.cw_state);
  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssRuntimeState UnicoreFreqJamStatusToRuntimeState(
    const UnicoreFreqJamStatusRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;

  UnicoreJammingState aggregate = UnicoreJammingState::kUnknown;
  const std::array<UnicoreJammingState, 3u> states{
      record.l1.cw_state,
      record.l2.cw_state,
      record.l5.cw_state,
  };
  for (const auto state_value : states)
  {
    if (state_value == UnicoreJammingState::kStrongJamming)
    {
      aggregate = state_value;
      break;
    }
    if (state_value == UnicoreJammingState::kJamming)
    {
      aggregate = state_value;
      continue;
    }
    if (state_value == UnicoreJammingState::kNone &&
        aggregate == UnicoreJammingState::kUnknown)
    {
      aggregate = state_value;
    }
  }

  ApplyJammingState(state, aggregate);
  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssDiagnosticEvent UnicoreJamStatusToDiagnosticEvent(
    const UnicoreJamStatusRecord& record)
{
  using universal_gnss::GnssDiagnosticSeverity;

  switch (record.cw_state)
  {
    case UnicoreJammingState::kNone:
      return BuildReceiverDiagnostic(GnssDiagnosticSeverity::kOk,
                                     "unicore_jam_status.clear",
                                     "Unicore receiver reports no jamming detected",
                                     record.header.timestamp_ns,
                                     "unicore/JAMSTATUSA");
    case UnicoreJammingState::kJamming:
      return BuildReceiverDiagnostic(GnssDiagnosticSeverity::kWarning,
                                     "unicore_jam_status.detected",
                                     "Unicore receiver reports jamming detected",
                                     record.header.timestamp_ns,
                                     "unicore/JAMSTATUSA");
    case UnicoreJammingState::kStrongJamming:
      return BuildReceiverDiagnostic(GnssDiagnosticSeverity::kError,
                                     "unicore_jam_status.strong",
                                     "Unicore receiver reports strong jamming detected",
                                     record.header.timestamp_ns,
                                     "unicore/JAMSTATUSA");
    case UnicoreJammingState::kUnknown:
    default:
      return BuildReceiverDiagnostic(GnssDiagnosticSeverity::kUnknown,
                                     "unicore_jam_status.unknown",
                                     "Unicore receiver reports an unknown jamming state",
                                     record.header.timestamp_ns,
                                     "unicore/JAMSTATUSA");
  }
}

universal_gnss::GnssDiagnosticEvent UnicoreFreqJamStatusToDiagnosticEvent(
    const UnicoreFreqJamStatusRecord& record)
{
  using universal_gnss::GnssDiagnosticSeverity;

  const std::array<std::pair<const char*, UnicoreJammingState>, 3u> bands{{
      {"L1", record.l1.cw_state},
      {"L2", record.l2.cw_state},
      {"L5", record.l5.cw_state},
  }};

  bool any_known = false;
  bool any_jam = false;
  bool any_strong = false;
  std::string affected_bands;
  for (const auto& band : bands)
  {
    if (band.second == UnicoreJammingState::kUnknown)
    {
      continue;
    }

    any_known = true;
    if (band.second == UnicoreJammingState::kJamming ||
        band.second == UnicoreJammingState::kStrongJamming)
    {
      if (!affected_bands.empty())
      {
        affected_bands += ", ";
      }
      affected_bands += band.first;
      any_jam = true;
      if (band.second == UnicoreJammingState::kStrongJamming)
      {
        any_strong = true;
      }
    }
  }

  if (!any_known)
  {
    return BuildReceiverDiagnostic(GnssDiagnosticSeverity::kUnknown,
                                   "unicore_freq_jam_status.unknown",
                                   "Unicore receiver reports unknown per-frequency jamming state",
                                   record.header.timestamp_ns,
                                   "unicore/FREQJAMSTATUSA");
  }
  if (any_strong)
  {
    return BuildReceiverDiagnostic(GnssDiagnosticSeverity::kError,
                                   "unicore_freq_jam_status.strong",
                                   "Unicore receiver reports strong jamming on " + affected_bands,
                                   record.header.timestamp_ns,
                                   "unicore/FREQJAMSTATUSA");
  }
  if (any_jam)
  {
    return BuildReceiverDiagnostic(GnssDiagnosticSeverity::kWarning,
                                   "unicore_freq_jam_status.detected",
                                   "Unicore receiver reports jamming on " + affected_bands,
                                   record.header.timestamp_ns,
                                   "unicore/FREQJAMSTATUSA");
  }

  return BuildReceiverDiagnostic(GnssDiagnosticSeverity::kOk,
                                 "unicore_freq_jam_status.clear",
                                 "Unicore receiver reports no per-frequency jamming detected",
                                 record.header.timestamp_ns,
                                 "unicore/FREQJAMSTATUSA");
}

universal_gnss::GnssDiagnosticEvent UnicoreHwStatusToDiagnosticEvent(
    const UnicoreHwStatusRecord& record)
{
  using universal_gnss::GnssDiagnosticSeverity;

  if (!record.clock_drift_valid)
  {
    return BuildReceiverDiagnostic(GnssDiagnosticSeverity::kWarning,
                                   "unicore_hw_status.clock_invalid",
                                   "Unicore hardware status reports invalid clock-drift data",
                                   record.header.timestamp_ns,
                                   "unicore/HWSTATUSA");
  }

  return BuildReceiverDiagnostic(GnssDiagnosticSeverity::kOk,
                                 "unicore_hw_status.ok",
                                 "Unicore hardware status reports valid clock-drift data",
                                 record.header.timestamp_ns,
                                 "unicore/HWSTATUSA");
}

}  // namespace universal_gnss_protocols
