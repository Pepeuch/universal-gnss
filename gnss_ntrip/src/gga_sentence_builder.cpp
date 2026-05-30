#include "universal_gnss_ntrip/gga_sentence_builder.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include "universal_gnss_protocols/nmea_checksum.hpp"

namespace universal_gnss_ntrip
{

namespace
{

struct FormattedCoordinate
{
  std::string value{};
  char hemisphere{'?'};
};

bool IsFinite(const double value)
{
  return std::isfinite(value);
}

std::string FormatFixedTrimmed(const double value, const int precision)
{
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(precision) << value;

  std::string text = stream.str();
  const std::size_t decimal = text.find('.');
  if (decimal == std::string::npos)
  {
    return text;
  }

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

std::optional<FormattedCoordinate> FormatCoordinate(const double coordinate_deg,
                                                    const bool latitude)
{
  if (!IsFinite(coordinate_deg))
  {
    return std::nullopt;
  }

  const double max_abs = latitude ? 90.0 : 180.0;
  if (coordinate_deg < -max_abs || coordinate_deg > max_abs)
  {
    return std::nullopt;
  }

  const double absolute_deg = std::fabs(coordinate_deg);
  std::uint32_t degrees_component =
      static_cast<std::uint32_t>(std::floor(absolute_deg));
  double minutes = (absolute_deg - static_cast<double>(degrees_component)) * 60.0;

  constexpr double kMinutePrecisionScale = 100000.0;
  minutes = std::round(minutes * kMinutePrecisionScale) / kMinutePrecisionScale;
  if (minutes >= 60.0)
  {
    minutes = 0.0;
    ++degrees_component;
  }

  if (degrees_component > static_cast<std::uint32_t>(max_abs))
  {
    return std::nullopt;
  }
  if (degrees_component == static_cast<std::uint32_t>(max_abs) && minutes > 0.0)
  {
    return std::nullopt;
  }

  std::ostringstream stream;
  stream << std::setfill('0')
         << std::setw(latitude ? 2 : 3) << degrees_component
         << std::fixed << std::setprecision(5)
         << std::setw(0) << minutes;

  std::string text = stream.str();
  if (minutes < 10.0)
  {
    text.insert(text.begin() + static_cast<std::ptrdiff_t>(latitude ? 2u : 3u), '0');
  }

  return FormattedCoordinate{
      text,
      coordinate_deg < 0.0 ? (latitude ? 'S' : 'W') : (latitude ? 'N' : 'E')};
}

std::string FormatSatelliteCount(const std::optional<std::uint16_t> satellites_used)
{
  const std::uint16_t count = satellites_used.value_or(0u);
  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(2) << count;
  return stream.str();
}

std::string FormatHdop(const std::optional<float> hdop)
{
  if (!hdop.has_value() || !std::isfinite(*hdop) || *hdop < 0.0f)
  {
    return {};
  }

  return FormatFixedTrimmed(static_cast<double>(*hdop), 2);
}

std::string FormatAltitude(const std::optional<double> altitude_m)
{
  if (!altitude_m.has_value() || !IsFinite(*altitude_m))
  {
    return {};
  }

  return FormatFixedTrimmed(*altitude_m, 3);
}

std::optional<std::string> FormatUtcTime(
    const std::optional<universal_gnss_protocols::NmeaUtcTime>& utc_time)
{
  if (!utc_time.has_value())
  {
    return std::string{"000000.00"};
  }

  if (!std::isfinite(utc_time->second) ||
      utc_time->hour > 23u ||
      utc_time->minute > 59u ||
      utc_time->second < 0.0 ||
      utc_time->second >= 60.0)
  {
    return std::nullopt;
  }

  std::uint32_t hour = utc_time->hour;
  std::uint32_t minute = utc_time->minute;
  std::uint32_t centiseconds =
      static_cast<std::uint32_t>(std::llround(utc_time->second * 100.0));
  if (centiseconds >= 6000u)
  {
    centiseconds = 0u;
    ++minute;
    if (minute >= 60u)
    {
      minute = 0u;
      hour = (hour + 1u) % 24u;
    }
  }

  const std::uint32_t second_whole = centiseconds / 100u;
  const std::uint32_t second_fraction = centiseconds % 100u;

  std::ostringstream stream;
  stream << std::setfill('0')
         << std::setw(2) << hour
         << std::setw(2) << minute
         << std::setw(2) << second_whole
         << '.'
         << std::setw(2) << second_fraction;
  return stream.str();
}

const char* SentenceIdForTalker(const GgaSentenceTalker talker)
{
  switch (talker)
  {
    case GgaSentenceTalker::kGn:
      return "GNGGA";
    case GgaSentenceTalker::kGp:
    default:
      return "GPGGA";
  }
}

std::string BuildSentenceFromPayload(const std::string& payload)
{
  const std::uint8_t checksum = universal_gnss_protocols::ComputeNmeaChecksum(payload);

  std::ostringstream stream;
  stream << '$' << payload << '*'
         << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
         << static_cast<unsigned int>(checksum) << "\r\n";
  return stream.str();
}

}  // namespace

bool GgaSentenceBuildResult::ok() const
{
  return error == GgaSentenceBuildError::kNone;
}

universal_gnss_protocols::NmeaGgaFixQuality MapRuntimeStateToGgaFixQuality(
    const universal_gnss::GnssRuntimeState& state)
{
  using universal_gnss::GnssFixType;
  using universal_gnss::GnssRtkMode;
  using universal_gnss_protocols::NmeaGgaFixQuality;

  if (!state.fix_valid || state.fix_type == GnssFixType::kNoFix)
  {
    return NmeaGgaFixQuality::kInvalid;
  }

  if (state.fix_type == GnssFixType::kRtkFixed ||
      state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFixed))
  {
    return NmeaGgaFixQuality::kRtkFixed;
  }

  if (state.fix_type == GnssFixType::kRtkFloat ||
      state.rtk_mode == std::optional<GnssRtkMode>(GnssRtkMode::kFloat))
  {
    return NmeaGgaFixQuality::kRtkFloat;
  }

  if (state.fix_type == GnssFixType::kDeadReckoning)
  {
    return NmeaGgaFixQuality::kEstimated;
  }

  return NmeaGgaFixQuality::kGpsFix;
}

GgaSentenceBuildResult BuildNmeaGgaSentence(
    const universal_gnss::GnssRuntimeState& state,
    const GgaSentenceBuilderOptions& options)
{
  GgaSentenceBuildResult result;
  result.fix_quality = MapRuntimeStateToGgaFixQuality(state);

  if (!state.latitude_deg.has_value())
  {
    result.error = GgaSentenceBuildError::kMissingLatitude;
    return result;
  }
  if (!state.longitude_deg.has_value())
  {
    result.error = GgaSentenceBuildError::kMissingLongitude;
    return result;
  }

  const auto latitude = FormatCoordinate(*state.latitude_deg, true);
  if (!latitude.has_value())
  {
    result.error = GgaSentenceBuildError::kInvalidLatitude;
    return result;
  }

  const auto longitude = FormatCoordinate(*state.longitude_deg, false);
  if (!longitude.has_value())
  {
    result.error = GgaSentenceBuildError::kInvalidLongitude;
    return result;
  }

  const auto utc_time = FormatUtcTime(options.utc_time);
  if (!utc_time.has_value())
  {
    result.error = GgaSentenceBuildError::kInvalidUtcTime;
    return result;
  }

  std::ostringstream payload;
  payload << SentenceIdForTalker(options.talker) << ','
          << *utc_time << ','
          << latitude->value << ',' << latitude->hemisphere << ','
          << longitude->value << ',' << longitude->hemisphere << ','
          << static_cast<unsigned int>(result.fix_quality) << ','
          << FormatSatelliteCount(state.satellites_used) << ','
          << FormatHdop(state.hdop) << ','
          << FormatAltitude(state.altitude_m) << ",M,,,,";

  result.sentence = BuildSentenceFromPayload(payload.str());
  return result;
}

}  // namespace universal_gnss_ntrip
