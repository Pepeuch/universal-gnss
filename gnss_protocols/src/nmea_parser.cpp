#include "universal_gnss_protocols/nmea_parser.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

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
    start = comma + 1;

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

bool TryParseDouble(std::string_view text, double& value)
{
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

bool TryParseUnsigned(std::string_view text, unsigned int& value)
{
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

OptionalFieldStatus ParseOptionalFloat(std::string_view text, std::optional<float>& value)
{
  value.reset();
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

OptionalFieldStatus ParseOptionalUtcTime(std::string_view text, std::optional<NmeaUtcTime>& value)
{
  value.reset();
  if (text.empty())
  {
    return OptionalFieldStatus::kMissing;
  }

  if (text.size() < 6)
  {
    return OptionalFieldStatus::kInvalid;
  }

  unsigned int hour = 0;
  unsigned int minute = 0;
  if (!TryParseUnsigned(text.substr(0, 2), hour) || !TryParseUnsigned(text.substr(2, 2), minute))
  {
    return OptionalFieldStatus::kInvalid;
  }

  double seconds = 0.0;
  if (!TryParseDouble(text.substr(4), seconds))
  {
    return OptionalFieldStatus::kInvalid;
  }

  if (hour > 23u || minute > 59u || seconds < 0.0 || seconds >= 61.0)
  {
    return OptionalFieldStatus::kInvalid;
  }

  value = NmeaUtcTime{static_cast<std::uint8_t>(hour), static_cast<std::uint8_t>(minute), seconds};
  return OptionalFieldStatus::kValue;
}

OptionalFieldStatus ParseOptionalDate(std::string_view text, std::optional<NmeaDate>& value)
{
  value.reset();
  if (text.empty())
  {
    return OptionalFieldStatus::kMissing;
  }

  if (text.size() != 6)
  {
    return OptionalFieldStatus::kInvalid;
  }

  unsigned int day = 0;
  unsigned int month = 0;
  unsigned int year = 0;
  if (!TryParseUnsigned(text.substr(0, 2), day) || !TryParseUnsigned(text.substr(2, 2), month) ||
      !TryParseUnsigned(text.substr(4, 2), year))
  {
    return OptionalFieldStatus::kInvalid;
  }

  if (day == 0u || day > 31u || month == 0u || month > 12u)
  {
    return OptionalFieldStatus::kInvalid;
  }

  value = NmeaDate{
      static_cast<std::uint8_t>(day), static_cast<std::uint8_t>(month),
      static_cast<std::uint8_t>(year)};
  return OptionalFieldStatus::kValue;
}

OptionalFieldStatus ParseOptionalCoordinate(std::string_view value_field,
                                            std::string_view hemisphere_field,
                                            std::size_t degree_digits,
                                            std::optional<double>& value,
                                            char positive_hemisphere,
                                            char negative_hemisphere)
{
  value.reset();
  if (value_field.empty() && hemisphere_field.empty())
  {
    return OptionalFieldStatus::kMissing;
  }
  if (value_field.empty() || hemisphere_field.size() != 1u)
  {
    return OptionalFieldStatus::kInvalid;
  }

  std::optional<double> parsed = ParseNmeaDegreesMinutes(value_field, degree_digits);
  if (!parsed.has_value())
  {
    return OptionalFieldStatus::kInvalid;
  }

  const char hemisphere = hemisphere_field.front();
  if (hemisphere == negative_hemisphere)
  {
    value = -*parsed;
  }
  else if (hemisphere == positive_hemisphere)
  {
    value = *parsed;
  }
  else
  {
    return OptionalFieldStatus::kInvalid;
  }

  return OptionalFieldStatus::kValue;
}

bool ParseGgaFixQuality(std::string_view text, NmeaGgaFixQuality& fix_quality)
{
  unsigned int raw_fix_quality = 0;
  if (!TryParseUnsigned(text, raw_fix_quality) || raw_fix_quality > 8u)
  {
    return false;
  }

  fix_quality = static_cast<NmeaGgaFixQuality>(raw_fix_quality);
  return true;
}

ParserResult<NmeaGgaRecord> InvalidGga()
{
  return ParserResult<NmeaGgaRecord>::InvalidData();
}

ParserResult<NmeaRmcRecord> InvalidRmc()
{
  return ParserResult<NmeaRmcRecord>::InvalidData();
}

}  // namespace

bool IsNmeaSentenceType(const NmeaSentence& sentence, std::string_view sentence_type)
{
  return sentence.sentence_type == sentence_type;
}

std::optional<double> ParseNmeaDegreesMinutes(std::string_view field, std::size_t degree_digits)
{
  if (field.empty())
  {
    return std::nullopt;
  }

  const std::size_t decimal_point = field.find('.');
  const std::size_t digits_before_decimal =
      decimal_point == std::string_view::npos ? field.size() : decimal_point;
  if (digits_before_decimal < degree_digits + 2u)
  {
    return std::nullopt;
  }

  unsigned int degrees = 0;
  if (!TryParseUnsigned(field.substr(0, degree_digits), degrees))
  {
    return std::nullopt;
  }

  double minutes = 0.0;
  if (!TryParseDouble(field.substr(degree_digits), minutes))
  {
    return std::nullopt;
  }

  if (minutes < 0.0 || minutes >= 60.0)
  {
    return std::nullopt;
  }

  return static_cast<double>(degrees) + (minutes / 60.0);
}

std::optional<double> ParseNmeaLatitude(std::string_view field, std::string_view hemisphere)
{
  std::optional<double> latitude_deg;
  if (ParseOptionalCoordinate(field, hemisphere, 2u, latitude_deg, 'N', 'S') !=
      OptionalFieldStatus::kValue)
  {
    return std::nullopt;
  }

  if (*latitude_deg < -90.0 || *latitude_deg > 90.0)
  {
    return std::nullopt;
  }

  return latitude_deg;
}

std::optional<double> ParseNmeaLongitude(std::string_view field, std::string_view hemisphere)
{
  std::optional<double> longitude_deg;
  if (ParseOptionalCoordinate(field, hemisphere, 3u, longitude_deg, 'E', 'W') !=
      OptionalFieldStatus::kValue)
  {
    return std::nullopt;
  }

  if (*longitude_deg < -180.0 || *longitude_deg > 180.0)
  {
    return std::nullopt;
  }

  return longitude_deg;
}

ParserResult<NmeaGgaRecord> ParseNmeaGga(const NmeaSentence& sentence)
{
  if (!IsNmeaSentenceType(sentence, "GGA"))
  {
    return ParserResult<NmeaGgaRecord>::Skipped();
  }
  if (sentence.checksum_status != ChecksumStatus::kValid)
  {
    return InvalidGga();
  }

  std::array<std::string_view, 16> fields{};
  std::size_t field_count = 0;
  if (!TokenizeCsv(sentence.payload_text, fields, field_count) || field_count < 7u)
  {
    return InvalidGga();
  }

  NmeaGgaRecord record;
  record.timestamp_ns = sentence.timestamp_ns;

  if (ParseOptionalUtcTime(fields[1], record.utc_time) == OptionalFieldStatus::kInvalid)
  {
    return InvalidGga();
  }

  const OptionalFieldStatus latitude_status =
      field_count > 3u ? ParseOptionalCoordinate(fields[2], fields[3], 2u, record.latitude_deg, 'N', 'S')
                       : OptionalFieldStatus::kMissing;
  if (latitude_status == OptionalFieldStatus::kInvalid ||
      (record.latitude_deg.has_value() &&
       (*record.latitude_deg < -90.0 || *record.latitude_deg > 90.0)))
  {
    return InvalidGga();
  }

  const OptionalFieldStatus longitude_status =
      field_count > 5u ? ParseOptionalCoordinate(fields[4], fields[5], 3u, record.longitude_deg, 'E', 'W')
                       : OptionalFieldStatus::kMissing;
  if (longitude_status == OptionalFieldStatus::kInvalid ||
      (record.longitude_deg.has_value() &&
       (*record.longitude_deg < -180.0 || *record.longitude_deg > 180.0)))
  {
    return InvalidGga();
  }

  if (!ParseGgaFixQuality(fields[6], record.fix_quality))
  {
    return InvalidGga();
  }
  record.fix_valid = record.fix_quality != NmeaGgaFixQuality::kInvalid;

  if (field_count > 7u &&
      ParseOptionalUnsigned16(fields[7], record.satellites_used) == OptionalFieldStatus::kInvalid)
  {
    return InvalidGga();
  }

  if (field_count > 8u && ParseOptionalFloat(fields[8], record.hdop) == OptionalFieldStatus::kInvalid)
  {
    return InvalidGga();
  }

  if (field_count > 9u)
  {
    if (ParseOptionalDouble(fields[9], record.altitude_m) == OptionalFieldStatus::kInvalid)
    {
      return InvalidGga();
    }

    if (record.altitude_m.has_value() && field_count > 10u && !fields[10].empty() &&
        fields[10] != "M")
    {
      return InvalidGga();
    }
  }

  return ParserResult<NmeaGgaRecord>::RecordReady(std::move(record));
}

ParserResult<NmeaRmcRecord> ParseNmeaRmc(const NmeaSentence& sentence)
{
  if (!IsNmeaSentenceType(sentence, "RMC"))
  {
    return ParserResult<NmeaRmcRecord>::Skipped();
  }
  if (sentence.checksum_status != ChecksumStatus::kValid)
  {
    return InvalidRmc();
  }

  std::array<std::string_view, 16> fields{};
  std::size_t field_count = 0;
  if (!TokenizeCsv(sentence.payload_text, fields, field_count) || field_count < 3u)
  {
    return InvalidRmc();
  }

  NmeaRmcRecord record;
  record.timestamp_ns = sentence.timestamp_ns;

  if (ParseOptionalUtcTime(fields[1], record.utc_time) == OptionalFieldStatus::kInvalid)
  {
    return InvalidRmc();
  }

  if (fields[2].size() != 1u || (fields[2] != "A" && fields[2] != "V"))
  {
    return InvalidRmc();
  }
  record.fix_valid = fields[2] == "A";

  if (field_count > 3u && field_count <= 6u)
  {
    return InvalidRmc();
  }

  if (field_count > 6u)
  {
    if (ParseOptionalCoordinate(fields[3], fields[4], 2u, record.latitude_deg, 'N', 'S') ==
            OptionalFieldStatus::kInvalid ||
        ParseOptionalCoordinate(fields[5], fields[6], 3u, record.longitude_deg, 'E', 'W') ==
            OptionalFieldStatus::kInvalid)
    {
      return InvalidRmc();
    }
  }

  if (record.latitude_deg.has_value() &&
      (*record.latitude_deg < -90.0 || *record.latitude_deg > 90.0))
  {
    return InvalidRmc();
  }
  if (record.longitude_deg.has_value() &&
      (*record.longitude_deg < -180.0 || *record.longitude_deg > 180.0))
  {
    return InvalidRmc();
  }

  if (field_count > 7u &&
      ParseOptionalFloat(fields[7], record.speed_over_ground_knots) == OptionalFieldStatus::kInvalid)
  {
    return InvalidRmc();
  }

  if (field_count > 8u &&
      ParseOptionalFloat(fields[8], record.course_over_ground_deg) == OptionalFieldStatus::kInvalid)
  {
    return InvalidRmc();
  }

  if (record.course_over_ground_deg.has_value() &&
      (*record.course_over_ground_deg < 0.0f || *record.course_over_ground_deg >= 360.0f))
  {
    return InvalidRmc();
  }

  if (field_count > 9u && ParseOptionalDate(fields[9], record.date) == OptionalFieldStatus::kInvalid)
  {
    return InvalidRmc();
  }

  return ParserResult<NmeaRmcRecord>::RecordReady(std::move(record));
}

universal_gnss::GnssRuntimeState NmeaGgaToRuntimeState(const NmeaGgaRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.timestamp_ns;
  state.fix_valid = record.fix_valid;
  state.fix_type = record.fix_valid ? universal_gnss::GnssFixType::kFix
                                    : universal_gnss::GnssFixType::kNoFix;
  state.latitude_deg = record.latitude_deg;
  state.longitude_deg = record.longitude_deg;
  state.altitude_m = record.altitude_m;

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHdop);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesUsed);

  if (record.hdop.has_value())
  {
    universal_gnss::SetOptionalValue(
        state, universal_gnss::GnssCapability::kHdop, state.hdop, *record.hdop);
  }
  if (record.satellites_used.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kSatellitesUsed,
                                     state.satellites_used,
                                     *record.satellites_used);
  }

  return state;
}

universal_gnss::GnssRuntimeState NmeaRmcToRuntimeState(const NmeaRmcRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.timestamp_ns;
  state.fix_valid = record.fix_valid;
  state.fix_type = record.fix_valid ? universal_gnss::GnssFixType::kFix
                                    : universal_gnss::GnssFixType::kNoFix;
  state.latitude_deg = record.latitude_deg;
  state.longitude_deg = record.longitude_deg;
  return state;
}

}  // namespace universal_gnss_protocols
