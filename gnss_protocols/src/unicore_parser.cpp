#include "universal_gnss_protocols/unicore_parser.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
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
  if (errno != 0 || end == nullptr || *end != '\0')
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

void SetHeading(universal_gnss::GnssRuntimeState& state,
                const UnicoreSolutionStatus heading_status,
                const std::optional<float> heading_deg)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHeading);
  if (heading_status == UnicoreSolutionStatus::kSolComputed && heading_deg.has_value())
  {
    universal_gnss::SetOptionalValue(
        state,
        universal_gnss::GnssCapability::kHeading,
        state.heading_deg,
        *heading_deg);
  }
}

void SetDualAntennaState(universal_gnss::GnssRuntimeState& state,
                         const UnicoreDualAntennaStatus status)
{
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kDualAntennaHeading);
  switch (status)
  {
    case UnicoreDualAntennaStatus::kWithinLimit:
      universal_gnss::SetOptionalValue(
          state,
          universal_gnss::GnssCapability::kDualAntennaHeading,
          state.dual_antenna_heading,
          true);
      break;
    case UnicoreDualAntennaStatus::kNotSolved:
    case UnicoreDualAntennaStatus::kOutOfLimit:
    case UnicoreDualAntennaStatus::kNotConfigured:
      universal_gnss::SetOptionalValue(
          state,
          universal_gnss::GnssCapability::kDualAntennaHeading,
          state.dual_antenna_heading,
          false);
      break;
    case UnicoreDualAntennaStatus::kUnknown:
    default:
      break;
  }
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
    record.heading_status = ParseSolutionStatus(fields[20]);
  }
  if (field_count > 21u &&
      ParseOptionalFloat(fields[21], record.heading_length_m) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 22u &&
      ParseOptionalFloat(fields[22], record.heading_deg) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 23u &&
      ParseOptionalFloat(fields[23], record.heading_pitch_deg) == OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 24u &&
      ParseOptionalUnsigned16(fields[24], record.heading_tracked_satellites) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicorePvtslnRecord>();
  }
  if (field_count > 25u &&
      ParseOptionalUnsigned16(fields[25], record.heading_used_satellites) ==
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
      ParseOptionalFloat(fields[11], record.diff_age_s) == OptionalFieldStatus::kInvalid ||
      ParseOptionalFloat(fields[12], record.solution_age_s) == OptionalFieldStatus::kInvalid ||
      ParseOptionalUnsigned16(fields[13], record.tracked_satellites) ==
          OptionalFieldStatus::kInvalid ||
      ParseOptionalUnsigned16(fields[14], record.used_satellites) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidResult<UnicoreBestNavRecord>();
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

universal_gnss::GnssRuntimeState UnicorePvtslnToRuntimeState(const UnicorePvtslnRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;

  ApplyFixType(state, record.best_position_type);
  ApplyRtkMode(state, record.best_position_type);
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

  SetHeading(state, record.heading_status, record.heading_deg);
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

universal_gnss::GnssRuntimeState UnicoreRtkStatusToRuntimeState(
    const UnicoreRtkStatusRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;

  ApplyFixType(state, record.position_type);
  ApplyRtkMode(state, record.position_type);
  SetDualAntennaState(state, record.dual_antenna_status);

  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

universal_gnss::GnssRuntimeState UnicoreRtcmStatusToRuntimeState(
    const UnicoreRtcmStatusRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.header.timestamp_ns;
  universal_gnss::RefreshValueFlagsFromFields(state);
  return state;
}

}  // namespace universal_gnss_protocols
