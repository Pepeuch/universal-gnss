#include "universal_gnss_protocols/nmea_parser.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
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
  if (errno != 0 || end == nullptr || *end != '\0' || !std::isfinite(parsed))
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

bool TryParseSigned(std::string_view text, int& value)
{
  if (text.empty())
  {
    return false;
  }

  std::string buffer(text);
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(buffer.c_str(), &end, 10);
  if (errno != 0 || end == nullptr || *end != '\0' ||
      parsed < static_cast<long>(std::numeric_limits<int>::min()) ||
      parsed > static_cast<long>(std::numeric_limits<int>::max()))
  {
    return false;
  }

  value = static_cast<int>(parsed);
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

std::optional<universal_gnss::GnssRtkMode> MapGgaFixQualityToRtkMode(
    const NmeaGgaFixQuality fix_quality)
{
  switch (fix_quality)
  {
    case NmeaGgaFixQuality::kRtkFixed:
      return universal_gnss::GnssRtkMode::kFixed;
    case NmeaGgaFixQuality::kRtkFloat:
      return universal_gnss::GnssRtkMode::kFloat;
    case NmeaGgaFixQuality::kInvalid:
    case NmeaGgaFixQuality::kGpsFix:
    case NmeaGgaFixQuality::kDifferentialFix:
    case NmeaGgaFixQuality::kPpsFix:
    case NmeaGgaFixQuality::kEstimated:
    case NmeaGgaFixQuality::kManual:
    case NmeaGgaFixQuality::kSimulation:
      return universal_gnss::GnssRtkMode::kNone;
  }

  return std::nullopt;
}

ParserResult<NmeaGgaRecord> InvalidGga()
{
  return ParserResult<NmeaGgaRecord>::InvalidData();
}

ParserResult<NmeaRmcRecord> InvalidRmc()
{
  return ParserResult<NmeaRmcRecord>::InvalidData();
}

ParserResult<NmeaGsaRecord> InvalidGsa()
{
  return ParserResult<NmeaGsaRecord>::InvalidData();
}

ParserResult<NmeaGsvRecord> InvalidGsv()
{
  return ParserResult<NmeaGsvRecord>::InvalidData();
}

ParserResult<NmeaGstRecord> InvalidGst()
{
  return ParserResult<NmeaGstRecord>::InvalidData();
}

ParserResult<NmeaVtgRecord> InvalidVtg()
{
  return ParserResult<NmeaVtgRecord>::InvalidData();
}

ParserResult<NmeaZdaRecord> InvalidZda()
{
  return ParserResult<NmeaZdaRecord>::InvalidData();
}

OptionalFieldStatus ParseOptionalPositiveFloat(std::string_view text, std::optional<float>& value)
{
  const OptionalFieldStatus status = ParseOptionalFloat(text, value);
  if (status == OptionalFieldStatus::kValue && value.has_value() && *value < 0.0f)
  {
    value.reset();
    return OptionalFieldStatus::kInvalid;
  }

  return status;
}

OptionalFieldStatus ParseOptionalUnsigned8(std::string_view text, std::optional<std::uint8_t>& value)
{
  value.reset();
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

OptionalFieldStatus ParseOptionalSigned8(std::string_view text, std::optional<std::int8_t>& value)
{
  value.reset();
  if (text.empty())
  {
    return OptionalFieldStatus::kMissing;
  }

  int parsed = 0;
  if (!TryParseSigned(text, parsed) ||
      parsed < static_cast<int>(std::numeric_limits<std::int8_t>::min()) ||
      parsed > static_cast<int>(std::numeric_limits<std::int8_t>::max()))
  {
    return OptionalFieldStatus::kInvalid;
  }

  value = static_cast<std::int8_t>(parsed);
  return OptionalFieldStatus::kValue;
}

bool ParseGsaMode(std::string_view text, NmeaGsaMode& fix_mode)
{
  if (text == "M")
  {
    fix_mode = NmeaGsaMode::kManual;
    return true;
  }
  if (text == "A")
  {
    fix_mode = NmeaGsaMode::kAutomatic;
    return true;
  }
  return false;
}

bool ParseFixDimension(std::string_view text, NmeaFixDimension& fix_dimension)
{
  unsigned int raw_dimension = 0;
  if (!TryParseUnsigned(text, raw_dimension) || raw_dimension < 1u || raw_dimension > 3u)
  {
    return false;
  }

  fix_dimension = static_cast<NmeaFixDimension>(raw_dimension);
  return true;
}

bool HasAnySatelliteBlockValue(const std::array<std::string_view, 4>& fields)
{
  for (const std::string_view field : fields)
  {
    if (!field.empty())
    {
      return true;
    }
  }
  return false;
}

bool ParseOptionalModeIndicator(std::string_view text, std::optional<char>& value)
{
  value.reset();
  if (text.empty())
  {
    return true;
  }
  if (text.size() != 1u)
  {
    return false;
  }

  const char mode = text.front();
  switch (mode)
  {
    case 'A':
    case 'D':
    case 'E':
    case 'M':
    case 'N':
    case 'S':
      value = mode;
      return true;
    default:
      return false;
  }
}

void UpdateFixDimensionInState(NmeaFixDimension fix_dimension,
                               universal_gnss::GnssRuntimeState& state)
{
  switch (fix_dimension)
  {
    case NmeaFixDimension::kNoFix:
      state.fix_valid = false;
      state.fix_type = universal_gnss::GnssFixType::kNoFix;
      universal_gnss::ClearPositionValues(state);
      break;
    case NmeaFixDimension::k2D:
    case NmeaFixDimension::k3D:
      state.fix_valid = true;
      if (state.fix_type == universal_gnss::GnssFixType::kUnknown ||
          state.fix_type == universal_gnss::GnssFixType::kNoFix)
      {
        state.fix_type = universal_gnss::GnssFixType::kFix;
      }
      break;
    case NmeaFixDimension::kUnknown:
      break;
  }
}

}  // namespace

bool IsNmeaSentenceType(const NmeaSentence& sentence, std::string_view sentence_type)
{
  return sentence.sentence_type == sentence_type;
}

bool IsNmeaGst(const NmeaSentence& sentence)
{
  return IsNmeaSentenceType(sentence, "GST");
}

bool IsNmeaVtg(const NmeaSentence& sentence)
{
  return IsNmeaSentenceType(sentence, "VTG");
}

bool IsNmeaZda(const NmeaSentence& sentence)
{
  return IsNmeaSentenceType(sentence, "ZDA");
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

ParserResult<NmeaGsaRecord> ParseNmeaGsa(const NmeaSentence& sentence)
{
  if (!IsNmeaSentenceType(sentence, "GSA"))
  {
    return ParserResult<NmeaGsaRecord>::Skipped();
  }
  if (sentence.checksum_status != ChecksumStatus::kValid)
  {
    return InvalidGsa();
  }

  std::array<std::string_view, 24> fields{};
  std::size_t field_count = 0;
  if (!TokenizeCsv(sentence.payload_text, fields, field_count) || field_count < 3u)
  {
    return InvalidGsa();
  }

  NmeaGsaRecord record;
  record.timestamp_ns = sentence.timestamp_ns;

  if (!ParseGsaMode(fields[1], record.fix_mode) ||
      !ParseFixDimension(fields[2], record.fix_dimension))
  {
    return InvalidGsa();
  }

  for (std::size_t field_index = 3u;
       field_index < field_count && field_index < 15u &&
       record.active_satellite_count < record.active_satellite_prns.size();
       ++field_index)
  {
    std::optional<std::uint16_t> prn;
    const OptionalFieldStatus status = ParseOptionalUnsigned16(fields[field_index], prn);
    if (status == OptionalFieldStatus::kInvalid)
    {
      return InvalidGsa();
    }
    if (status == OptionalFieldStatus::kValue && prn.has_value())
    {
      record.active_satellite_prns[record.active_satellite_count++] = *prn;
    }
  }

  if (field_count > 15u &&
      ParseOptionalPositiveFloat(fields[15], record.pdop) == OptionalFieldStatus::kInvalid)
  {
    return InvalidGsa();
  }
  if (field_count > 16u &&
      ParseOptionalPositiveFloat(fields[16], record.hdop) == OptionalFieldStatus::kInvalid)
  {
    return InvalidGsa();
  }
  if (field_count > 17u &&
      ParseOptionalPositiveFloat(fields[17], record.vdop) == OptionalFieldStatus::kInvalid)
  {
    return InvalidGsa();
  }

  return ParserResult<NmeaGsaRecord>::RecordReady(std::move(record));
}

ParserResult<NmeaGsvRecord> ParseNmeaGsv(const NmeaSentence& sentence)
{
  if (!IsNmeaSentenceType(sentence, "GSV"))
  {
    return ParserResult<NmeaGsvRecord>::Skipped();
  }
  if (sentence.checksum_status != ChecksumStatus::kValid)
  {
    return InvalidGsv();
  }

  std::array<std::string_view, 24> fields{};
  std::size_t field_count = 0;
  if (!TokenizeCsv(sentence.payload_text, fields, field_count) || field_count < 4u)
  {
    return InvalidGsv();
  }

  NmeaGsvRecord record;
  record.timestamp_ns = sentence.timestamp_ns;

  unsigned int total_messages = 0;
  unsigned int message_index = 0;
  unsigned int satellites_in_view = 0;
  if (!TryParseUnsigned(fields[1], total_messages) || !TryParseUnsigned(fields[2], message_index) ||
      !TryParseUnsigned(fields[3], satellites_in_view) || total_messages == 0u || message_index == 0u ||
      message_index > total_messages ||
      total_messages > static_cast<unsigned int>(std::numeric_limits<std::uint8_t>::max()) ||
      message_index > static_cast<unsigned int>(std::numeric_limits<std::uint8_t>::max()) ||
      satellites_in_view > static_cast<unsigned int>(std::numeric_limits<std::uint16_t>::max()))
  {
    return InvalidGsv();
  }

  record.total_messages = static_cast<std::uint8_t>(total_messages);
  record.message_index = static_cast<std::uint8_t>(message_index);
  record.satellites_in_view = static_cast<std::uint16_t>(satellites_in_view);

  for (std::size_t start = 4u;
       start + 3u < field_count && record.satellite_count < record.satellites.size();
       start += 4u)
  {
    const std::array<std::string_view, 4> satellite_fields = {
        fields[start], fields[start + 1u], fields[start + 2u], fields[start + 3u]};
    if (!HasAnySatelliteBlockValue(satellite_fields))
    {
      continue;
    }

    NmeaGsvSatellite satellite;
    std::optional<std::uint16_t> prn;
    std::optional<std::uint8_t> elevation;
    std::optional<std::uint16_t> azimuth;
    std::optional<float> cn0;

    if (ParseOptionalUnsigned16(satellite_fields[0], prn) == OptionalFieldStatus::kInvalid ||
        ParseOptionalUnsigned8(satellite_fields[1], elevation) == OptionalFieldStatus::kInvalid ||
        ParseOptionalUnsigned16(satellite_fields[2], azimuth) == OptionalFieldStatus::kInvalid ||
        ParseOptionalPositiveFloat(satellite_fields[3], cn0) == OptionalFieldStatus::kInvalid)
    {
      return InvalidGsv();
    }

    if (elevation.has_value() && *elevation > 90u)
    {
      return InvalidGsv();
    }
    if (azimuth.has_value() && *azimuth >= 360u)
    {
      return InvalidGsv();
    }

    satellite.prn = prn;
    satellite.elevation_deg = elevation;
    satellite.azimuth_deg = azimuth;
    satellite.cn0_db_hz = cn0;
    record.satellites[record.satellite_count++] = satellite;
  }

  return ParserResult<NmeaGsvRecord>::RecordReady(std::move(record));
}

ParserResult<NmeaGstRecord> ParseNmeaGst(const NmeaSentence& sentence)
{
  if (!IsNmeaGst(sentence))
  {
    return ParserResult<NmeaGstRecord>::Skipped();
  }
  if (sentence.checksum_status != ChecksumStatus::kValid)
  {
    return InvalidGst();
  }

  std::array<std::string_view, 16> fields{};
  std::size_t field_count = 0;
  if (!TokenizeCsv(sentence.payload_text, fields, field_count) || field_count < 2u)
  {
    return InvalidGst();
  }

  NmeaGstRecord record;
  record.timestamp_ns = sentence.timestamp_ns;

  if (ParseOptionalUtcTime(fields[1], record.utc_time) == OptionalFieldStatus::kInvalid)
  {
    return InvalidGst();
  }

  if (field_count > 2u &&
      ParseOptionalPositiveFloat(fields[2], record.rms_range_residual_m) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidGst();
  }
  if (field_count > 3u &&
      ParseOptionalPositiveFloat(fields[3], record.semi_major_std_dev_m) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidGst();
  }
  if (field_count > 4u &&
      ParseOptionalPositiveFloat(fields[4], record.semi_minor_std_dev_m) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidGst();
  }
  if (field_count > 5u &&
      ParseOptionalFloat(fields[5], record.orientation_deg) == OptionalFieldStatus::kInvalid)
  {
    return InvalidGst();
  }
  if (record.orientation_deg.has_value() &&
      (*record.orientation_deg < 0.0f || *record.orientation_deg >= 360.0f))
  {
    return InvalidGst();
  }
  if (field_count > 6u &&
      ParseOptionalPositiveFloat(fields[6], record.latitude_std_dev_m) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidGst();
  }
  if (field_count > 7u &&
      ParseOptionalPositiveFloat(fields[7], record.longitude_std_dev_m) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidGst();
  }
  if (field_count > 8u &&
      ParseOptionalPositiveFloat(fields[8], record.altitude_std_dev_m) ==
          OptionalFieldStatus::kInvalid)
  {
    return InvalidGst();
  }

  return ParserResult<NmeaGstRecord>::RecordReady(std::move(record));
}

ParserResult<NmeaVtgRecord> ParseNmeaVtg(const NmeaSentence& sentence)
{
  if (!IsNmeaVtg(sentence))
  {
    return ParserResult<NmeaVtgRecord>::Skipped();
  }
  if (sentence.checksum_status != ChecksumStatus::kValid)
  {
    return InvalidVtg();
  }

  std::array<std::string_view, 16> fields{};
  std::size_t field_count = 0;
  if (!TokenizeCsv(sentence.payload_text, fields, field_count) || field_count < 1u)
  {
    return InvalidVtg();
  }

  NmeaVtgRecord record;
  record.timestamp_ns = sentence.timestamp_ns;

  if (field_count > 1u &&
      ParseOptionalFloat(fields[1], record.true_course_deg) == OptionalFieldStatus::kInvalid)
  {
    return InvalidVtg();
  }
  if (record.true_course_deg.has_value() &&
      (*record.true_course_deg < 0.0f || *record.true_course_deg >= 360.0f))
  {
    return InvalidVtg();
  }
  if (field_count > 2u && !fields[2].empty() && fields[2] != "T")
  {
    return InvalidVtg();
  }

  if (field_count > 3u &&
      ParseOptionalFloat(fields[3], record.magnetic_course_deg) == OptionalFieldStatus::kInvalid)
  {
    return InvalidVtg();
  }
  if (record.magnetic_course_deg.has_value() &&
      (*record.magnetic_course_deg < 0.0f || *record.magnetic_course_deg >= 360.0f))
  {
    return InvalidVtg();
  }
  if (field_count > 4u && !fields[4].empty() && fields[4] != "M")
  {
    return InvalidVtg();
  }

  if (field_count > 5u &&
      ParseOptionalPositiveFloat(fields[5], record.speed_knots) == OptionalFieldStatus::kInvalid)
  {
    return InvalidVtg();
  }
  if (field_count > 6u && !fields[6].empty() && fields[6] != "N")
  {
    return InvalidVtg();
  }

  if (field_count > 7u &&
      ParseOptionalPositiveFloat(fields[7], record.speed_kmh) == OptionalFieldStatus::kInvalid)
  {
    return InvalidVtg();
  }
  if (field_count > 8u && !fields[8].empty() && fields[8] != "K")
  {
    return InvalidVtg();
  }

  if (field_count > 9u && !ParseOptionalModeIndicator(fields[9], record.mode_indicator))
  {
    return InvalidVtg();
  }

  return ParserResult<NmeaVtgRecord>::RecordReady(std::move(record));
}

ParserResult<NmeaZdaRecord> ParseNmeaZda(const NmeaSentence& sentence)
{
  if (!IsNmeaZda(sentence))
  {
    return ParserResult<NmeaZdaRecord>::Skipped();
  }
  if (sentence.checksum_status != ChecksumStatus::kValid)
  {
    return InvalidZda();
  }

  std::array<std::string_view, 12> fields{};
  std::size_t field_count = 0;
  if (!TokenizeCsv(sentence.payload_text, fields, field_count) || field_count < 5u)
  {
    return InvalidZda();
  }

  NmeaZdaRecord record;
  record.timestamp_ns = sentence.timestamp_ns;

  if (ParseOptionalUtcTime(fields[1], record.utc_time) != OptionalFieldStatus::kValue)
  {
    return InvalidZda();
  }

  unsigned int day = 0;
  unsigned int month = 0;
  unsigned int year = 0;
  if (!TryParseUnsigned(fields[2], day) || !TryParseUnsigned(fields[3], month) ||
      !TryParseUnsigned(fields[4], year) || day == 0u || day > 31u || month == 0u ||
      month > 12u || year < 1000u || year > 9999u)
  {
    return InvalidZda();
  }

  record.day = static_cast<std::uint8_t>(day);
  record.month = static_cast<std::uint8_t>(month);
  record.year = static_cast<std::uint16_t>(year);

  if (field_count > 5u || field_count > 6u)
  {
    const OptionalFieldStatus hours_status =
        field_count > 5u ? ParseOptionalSigned8(fields[5], record.local_zone_hours)
                         : OptionalFieldStatus::kMissing;
    const OptionalFieldStatus minutes_status =
        field_count > 6u ? ParseOptionalSigned8(fields[6], record.local_zone_minutes)
                         : OptionalFieldStatus::kMissing;

    if (hours_status == OptionalFieldStatus::kInvalid ||
        minutes_status == OptionalFieldStatus::kInvalid)
    {
      return InvalidZda();
    }

    const bool any_zone_field_present =
        hours_status == OptionalFieldStatus::kValue || minutes_status == OptionalFieldStatus::kValue;
    if (any_zone_field_present &&
        (hours_status != OptionalFieldStatus::kValue || minutes_status != OptionalFieldStatus::kValue))
    {
      return InvalidZda();
    }

    if (record.local_zone_hours.has_value() &&
        (*record.local_zone_hours < -13 || *record.local_zone_hours > 13))
    {
      return InvalidZda();
    }
    if (record.local_zone_minutes.has_value() &&
        (*record.local_zone_minutes < 0 || *record.local_zone_minutes > 59))
    {
      return InvalidZda();
    }
  }

  return ParserResult<NmeaZdaRecord>::RecordReady(std::move(record));
}

universal_gnss::GnssRuntimeState NmeaGgaToRuntimeState(const NmeaGgaRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.timestamp_ns;
  state.fix_valid = record.fix_valid;
  state.fix_type = record.fix_valid ? universal_gnss::GnssFixType::kFix
                                    : universal_gnss::GnssFixType::kNoFix;
  if (record.fix_valid)
  {
    state.latitude_deg = record.latitude_deg;
    state.longitude_deg = record.longitude_deg;
    state.altitude_m = record.altitude_m;
  }
  else
  {
    universal_gnss::ClearPositionValues(state);
  }

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kRtkMode);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHdop);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesUsed);

  if (const auto rtk_mode = MapGgaFixQualityToRtkMode(record.fix_quality); rtk_mode.has_value())
  {
    universal_gnss::SetOptionalValue(
        state, universal_gnss::GnssCapability::kRtkMode, state.rtk_mode, *rtk_mode);
  }
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
  if (record.fix_valid)
  {
    state.latitude_deg = record.latitude_deg;
    state.longitude_deg = record.longitude_deg;
  }
  else
  {
    universal_gnss::ClearPositionValues(state);
  }

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSpeedOverGround);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kCourseOverGround);
  if (record.fix_valid && record.speed_over_ground_knots.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kSpeedOverGround,
                                     state.speed_over_ground_m_s,
                                     *record.speed_over_ground_knots * 0.514444f);
  }
  else
  {
    universal_gnss::ClearOptionalValue(state,
                                       universal_gnss::GnssCapability::kSpeedOverGround,
                                       state.speed_over_ground_m_s);
  }
  if (record.fix_valid && record.course_over_ground_deg.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kCourseOverGround,
                                     state.course_over_ground_deg,
                                     *record.course_over_ground_deg);
  }
  else
  {
    universal_gnss::ClearOptionalValue(state,
                                       universal_gnss::GnssCapability::kCourseOverGround,
                                       state.course_over_ground_deg);
  }
  return state;
}

universal_gnss::GnssRuntimeState NmeaVtgToRuntimeState(const NmeaVtgRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  state.timestamp_ns = record.timestamp_ns;
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSpeedOverGround);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kCourseOverGround);

  if (record.speed_knots.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kSpeedOverGround,
                                     state.speed_over_ground_m_s,
                                     *record.speed_knots * 0.514444f);
  }
  else if (record.speed_kmh.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kSpeedOverGround,
                                     state.speed_over_ground_m_s,
                                     *record.speed_kmh / 3.6f);
  }
  else
  {
    universal_gnss::ClearOptionalValue(state,
                                       universal_gnss::GnssCapability::kSpeedOverGround,
                                       state.speed_over_ground_m_s);
  }

  if (record.true_course_deg.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kCourseOverGround,
                                     state.course_over_ground_deg,
                                     *record.true_course_deg);
  }
  else
  {
    universal_gnss::ClearOptionalValue(state,
                                       universal_gnss::GnssCapability::kCourseOverGround,
                                       state.course_over_ground_deg);
  }
  return state;
}

universal_gnss::GnssRuntimeState NmeaGsaToRuntimeState(const NmeaGsaRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  MergeNmeaGsaIntoRuntimeState(record, state);
  return state;
}

universal_gnss::GnssRuntimeState NmeaGstToRuntimeState(const NmeaGstRecord& record)
{
  universal_gnss::GnssRuntimeState state;
  MergeNmeaGstIntoRuntimeState(record, state);
  return state;
}

void MergeNmeaGsaIntoRuntimeState(const NmeaGsaRecord& record,
                                  universal_gnss::GnssRuntimeState& state)
{
  if (record.timestamp_ns.has_value())
  {
    state.timestamp_ns = record.timestamp_ns;
  }

  UpdateFixDimensionInState(record.fix_dimension, state);

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHdop);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kVdop);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesUsed);

  if (record.hdop.has_value())
  {
    universal_gnss::SetOptionalValue(
        state, universal_gnss::GnssCapability::kHdop, state.hdop, *record.hdop);
  }
  else
  {
    universal_gnss::ClearOptionalValue(state, universal_gnss::GnssCapability::kHdop, state.hdop);
  }

  if (record.vdop.has_value())
  {
    universal_gnss::SetOptionalValue(
        state, universal_gnss::GnssCapability::kVdop, state.vdop, *record.vdop);
  }
  else
  {
    universal_gnss::ClearOptionalValue(state, universal_gnss::GnssCapability::kVdop, state.vdop);
  }

  if (record.active_satellite_count > 0u)
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kSatellitesUsed,
                                     state.satellites_used,
                                     static_cast<std::uint16_t>(record.active_satellite_count));
  }
  else
  {
    universal_gnss::ClearOptionalValue(
        state, universal_gnss::GnssCapability::kSatellitesUsed, state.satellites_used);
  }
}

void MergeNmeaGsvIntoRuntimeState(const NmeaGsvRecord& record,
                                  universal_gnss::GnssRuntimeState& state)
{
  if (record.timestamp_ns.has_value())
  {
    state.timestamp_ns = record.timestamp_ns;
  }

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kSatellitesVisible);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kMeanCn0);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kMaxCn0);

  universal_gnss::SetOptionalValue(state,
                                   universal_gnss::GnssCapability::kSatellitesVisible,
                                   state.satellites_visible,
                                   record.satellites_in_view);

  float cn0_sum = 0.0f;
  float cn0_max = 0.0f;
  std::size_t cn0_count = 0;
  for (std::size_t index = 0; index < record.satellite_count; ++index)
  {
    const auto& satellite = record.satellites[index];
    if (!satellite.cn0_db_hz.has_value())
    {
      continue;
    }

    cn0_sum += *satellite.cn0_db_hz;
    cn0_max = (cn0_count == 0u || *satellite.cn0_db_hz > cn0_max) ? *satellite.cn0_db_hz : cn0_max;
    ++cn0_count;
  }

  if (cn0_count > 0u)
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kMeanCn0,
                                     state.mean_cn0_db_hz,
                                     cn0_sum / static_cast<float>(cn0_count));
    universal_gnss::SetOptionalValue(
        state, universal_gnss::GnssCapability::kMaxCn0, state.max_cn0_db_hz, cn0_max);
  }
  else
  {
    universal_gnss::ClearOptionalValue(
        state, universal_gnss::GnssCapability::kMeanCn0, state.mean_cn0_db_hz);
    universal_gnss::ClearOptionalValue(
        state, universal_gnss::GnssCapability::kMaxCn0, state.max_cn0_db_hz);
  }
}

void MergeNmeaGstIntoRuntimeState(const NmeaGstRecord& record,
                                  universal_gnss::GnssRuntimeState& state)
{
  if (record.timestamp_ns.has_value())
  {
    state.timestamp_ns = record.timestamp_ns;
  }

  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kHorizontalAccuracy);
  universal_gnss::SetCapability(state, universal_gnss::GnssCapability::kVerticalAccuracy);

  if (record.latitude_std_dev_m.has_value() && record.longitude_std_dev_m.has_value())
  {
    // GST carries 1-sigma latitude/longitude standard deviations. Use the worst
    // horizontal axis as a conservative single-value summary.
    const float horizontal_accuracy_m =
        std::max(*record.latitude_std_dev_m, *record.longitude_std_dev_m);
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kHorizontalAccuracy,
                                     state.horizontal_accuracy_m,
                                     horizontal_accuracy_m);
  }
  else
  {
    universal_gnss::ClearOptionalValue(state,
                                       universal_gnss::GnssCapability::kHorizontalAccuracy,
                                       state.horizontal_accuracy_m);
  }

  if (record.altitude_std_dev_m.has_value())
  {
    universal_gnss::SetOptionalValue(state,
                                     universal_gnss::GnssCapability::kVerticalAccuracy,
                                     state.vertical_accuracy_m,
                                     *record.altitude_std_dev_m);
  }
  else
  {
    universal_gnss::ClearOptionalValue(state,
                                       universal_gnss::GnssCapability::kVerticalAccuracy,
                                       state.vertical_accuracy_m);
  }
}

}  // namespace universal_gnss_protocols
