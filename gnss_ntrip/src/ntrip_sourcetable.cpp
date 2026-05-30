#include "universal_gnss_ntrip/ntrip_sourcetable.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace universal_gnss_ntrip
{

namespace
{

std::string_view TrimField(std::string_view text)
{
  while (!text.empty() &&
         (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
          text.front() == '"'))
  {
    text.remove_prefix(1u);
  }

  while (!text.empty() &&
         (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
          text.back() == '"'))
  {
    text.remove_suffix(1u);
  }

  return text;
}

std::vector<std::string_view> SplitSemicolonFields(std::string_view text)
{
  std::vector<std::string_view> fields;
  std::size_t start = 0u;

  while (start <= text.size())
  {
    const std::size_t semicolon = text.find(';', start);
    if (semicolon == std::string_view::npos)
    {
      fields.push_back(TrimField(text.substr(start)));
      break;
    }

    fields.push_back(TrimField(text.substr(start, semicolon - start)));
    start = semicolon + 1u;

    if (start == text.size())
    {
      fields.push_back(std::string_view{});
      break;
    }
  }

  return fields;
}

std::vector<std::string> CopyFields(const std::vector<std::string_view>& fields,
                                    const std::size_t first_field_index)
{
  std::vector<std::string> copied;
  if (first_field_index >= fields.size())
  {
    return copied;
  }

  copied.reserve(fields.size() - first_field_index);
  for (std::size_t index = first_field_index; index < fields.size(); ++index)
  {
    copied.emplace_back(fields[index]);
  }

  return copied;
}

std::string_view GetField(const std::vector<std::string_view>& fields, const std::size_t index)
{
  if (index >= fields.size())
  {
    return std::string_view{};
  }

  return fields[index];
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

bool TryParseBoolFlag(std::string_view text, bool& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    return false;
  }

  std::string upper;
  upper.reserve(text.size());
  for (const char ch : text)
  {
    if (ch >= 'a' && ch <= 'z')
    {
      upper.push_back(static_cast<char>(ch - ('a' - 'A')));
    }
    else
    {
      upper.push_back(ch);
    }
  }

  if (upper == "1" || upper == "Y" || upper == "YES" || upper == "TRUE")
  {
    value = true;
    return true;
  }

  if (upper == "0" || upper == "N" || upper == "NO" || upper == "FALSE")
  {
    value = false;
    return true;
  }

  return false;
}

void ParseOptionalString(std::string_view text, std::optional<std::string>& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    value.reset();
    return;
  }

  value = std::string(text);
}

bool ParseOptionalUnsigned32(std::string_view text, std::optional<std::uint32_t>& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    value.reset();
    return true;
  }

  unsigned int parsed = 0u;
  if (!TryParseUnsigned(text, parsed))
  {
    return false;
  }

  value = static_cast<std::uint32_t>(parsed);
  return true;
}

bool ParseOptionalUnsigned16(std::string_view text, std::optional<std::uint16_t>& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    value.reset();
    return true;
  }

  unsigned int parsed = 0u;
  if (!TryParseUnsigned(text, parsed) ||
      parsed > static_cast<unsigned int>(std::numeric_limits<std::uint16_t>::max()))
  {
    return false;
  }

  value = static_cast<std::uint16_t>(parsed);
  return true;
}

bool ParseOptionalDouble(std::string_view text, std::optional<double>& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    value.reset();
    return true;
  }

  double parsed = 0.0;
  if (!TryParseDouble(text, parsed))
  {
    return false;
  }

  value = parsed;
  return true;
}

bool ParseOptionalBool(std::string_view text, std::optional<bool>& value)
{
  text = TrimField(text);
  if (text.empty())
  {
    value.reset();
    return true;
  }

  bool parsed = false;
  if (!TryParseBoolFlag(text, parsed))
  {
    return false;
  }

  value = parsed;
  return true;
}

std::string_view NormalizeMountpointKey(std::string_view mountpoint)
{
  mountpoint = TrimField(mountpoint);
  while (!mountpoint.empty() && mountpoint.front() == '/')
  {
    mountpoint.remove_prefix(1u);
  }

  return mountpoint;
}

bool ContainsCaseInsensitive(std::string_view text, std::string_view needle)
{
  if (needle.empty())
  {
    return true;
  }

  for (std::size_t offset = 0u; offset + needle.size() <= text.size(); ++offset)
  {
    bool matches = true;
    for (std::size_t index = 0u; index < needle.size(); ++index)
    {
      char lhs = text[offset + index];
      char rhs = needle[index];
      if (lhs >= 'a' && lhs <= 'z')
      {
        lhs = static_cast<char>(lhs - ('a' - 'A'));
      }
      if (rhs >= 'a' && rhs <= 'z')
      {
        rhs = static_cast<char>(rhs - ('a' - 'A'));
      }

      if (lhs != rhs)
      {
        matches = false;
        break;
      }
    }

    if (matches)
    {
      return true;
    }
  }

  return false;
}

bool IsMsmMessageType(const std::uint32_t message_type)
{
  return (message_type >= 1071u && message_type <= 1077u) ||
         (message_type >= 1081u && message_type <= 1087u) ||
         (message_type >= 1091u && message_type <= 1097u) ||
         (message_type >= 1101u && message_type <= 1107u) ||
         (message_type >= 1111u && message_type <= 1117u) ||
         (message_type >= 1121u && message_type <= 1127u);
}

bool ContainsMsmMessageType(std::string_view text)
{
  std::size_t index = 0u;
  while (index < text.size())
  {
    if (text[index] < '0' || text[index] > '9')
    {
      ++index;
      continue;
    }

    std::size_t end = index + 1u;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9')
    {
      ++end;
    }

    unsigned int parsed = 0u;
    if (TryParseUnsigned(text.substr(index, end - index), parsed) &&
        IsMsmMessageType(parsed))
    {
      return true;
    }

    index = end;
  }

  return false;
}

void AddIssue(NtripSourcetable& sourcetable,
              const std::size_t line_number,
              const NtripSourcetableIssueCode code,
              std::string_view line)
{
  sourcetable.issues.push_back(NtripSourcetableIssue{
      line_number,
      code,
      std::string(line)});
}

bool ParseStreamRecord(const std::vector<std::string_view>& fields,
                       const std::size_t line_number,
                       const std::string_view line,
                       NtripSourcetable& sourcetable)
{
  const std::string_view mountpoint = GetField(fields, 1u);
  if (mountpoint.empty())
  {
    AddIssue(sourcetable,
             line_number,
             NtripSourcetableIssueCode::kMissingMountpoint,
             line);
    return false;
  }

  NtripSourcetableStream stream;
  stream.line_number = line_number;
  stream.mountpoint = std::string(mountpoint);
  stream.fields = CopyFields(fields, 1u);

  ParseOptionalString(GetField(fields, 2u), stream.identifier);
  ParseOptionalString(GetField(fields, 3u), stream.format);
  ParseOptionalString(GetField(fields, 4u), stream.format_details);
  if (!ParseOptionalUnsigned32(GetField(fields, 5u), stream.carrier))
  {
    AddIssue(sourcetable, line_number, NtripSourcetableIssueCode::kInvalidCarrier, line);
    return false;
  }
  ParseOptionalString(GetField(fields, 6u), stream.nav_system);
  ParseOptionalString(GetField(fields, 7u), stream.network);
  ParseOptionalString(GetField(fields, 8u), stream.country);
  if (!ParseOptionalDouble(GetField(fields, 9u), stream.latitude_deg))
  {
    AddIssue(sourcetable, line_number, NtripSourcetableIssueCode::kInvalidLatitude, line);
    return false;
  }
  if (!ParseOptionalDouble(GetField(fields, 10u), stream.longitude_deg))
  {
    AddIssue(sourcetable, line_number, NtripSourcetableIssueCode::kInvalidLongitude, line);
    return false;
  }
  if (!ParseOptionalBool(GetField(fields, 11u), stream.nmea_required))
  {
    AddIssue(sourcetable, line_number, NtripSourcetableIssueCode::kInvalidNmeaFlag, line);
    return false;
  }
  if (!ParseOptionalUnsigned32(GetField(fields, 12u), stream.solution))
  {
    AddIssue(sourcetable, line_number, NtripSourcetableIssueCode::kInvalidSolution, line);
    return false;
  }
  ParseOptionalString(GetField(fields, 13u), stream.generator);
  ParseOptionalString(GetField(fields, 14u), stream.compression);
  ParseOptionalString(GetField(fields, 15u), stream.authentication);
  if (!ParseOptionalBool(GetField(fields, 16u), stream.fee))
  {
    AddIssue(sourcetable, line_number, NtripSourcetableIssueCode::kInvalidFeeFlag, line);
    return false;
  }
  if (!ParseOptionalUnsigned32(GetField(fields, 17u), stream.bitrate))
  {
    AddIssue(sourcetable, line_number, NtripSourcetableIssueCode::kInvalidBitrate, line);
    return false;
  }

  sourcetable.streams.push_back(std::move(stream));
  return true;
}

bool ParseCasterRecord(const std::vector<std::string_view>& fields,
                       const std::size_t line_number,
                       const std::string_view line,
                       NtripSourcetable& sourcetable)
{
  NtripSourcetableCaster caster;
  caster.line_number = line_number;
  caster.fields = CopyFields(fields, 1u);

  ParseOptionalString(GetField(fields, 1u), caster.host);
  if (!ParseOptionalUnsigned16(GetField(fields, 2u), caster.port))
  {
    AddIssue(sourcetable, line_number, NtripSourcetableIssueCode::kInvalidCasterPort, line);
    return false;
  }
  ParseOptionalString(GetField(fields, 3u), caster.identifier);

  sourcetable.casters.push_back(std::move(caster));
  return true;
}

void ParseNetworkRecord(const std::vector<std::string_view>& fields,
                        const std::size_t line_number,
                        NtripSourcetable& sourcetable)
{
  NtripSourcetableNetwork network;
  network.line_number = line_number;
  network.fields = CopyFields(fields, 1u);
  ParseOptionalString(GetField(fields, 1u), network.identifier);
  sourcetable.networks.push_back(std::move(network));
}

}  // namespace

NtripSourcetable ParseNtripSourcetable(const std::string_view text)
{
  NtripSourcetable sourcetable;

  std::size_t line_start = 0u;
  std::size_t line_number = 0u;
  while (line_start <= text.size())
  {
    const std::size_t line_end = text.find('\n', line_start);
    const std::string_view raw_line =
        line_end == std::string_view::npos
            ? text.substr(line_start)
            : text.substr(line_start, line_end - line_start);

    ++line_number;
    const std::string_view line = TrimField(raw_line);
    if (!line.empty())
    {
      if (line == "ENDSOURCETABLE")
      {
        sourcetable.has_end_marker = true;
        break;
      }

      const std::vector<std::string_view> fields = SplitSemicolonFields(line);
      const std::string_view record_type = GetField(fields, 0u);
      if (record_type == "STR")
      {
        ParseStreamRecord(fields, line_number, line, sourcetable);
      }
      else if (record_type == "CAS")
      {
        ParseCasterRecord(fields, line_number, line, sourcetable);
      }
      else if (record_type == "NET")
      {
        ParseNetworkRecord(fields, line_number, sourcetable);
      }
      else
      {
        AddIssue(sourcetable, line_number, NtripSourcetableIssueCode::kUnknownRecord, line);
      }
    }

    if (line_end == std::string_view::npos)
    {
      break;
    }

    line_start = line_end + 1u;
  }

  return sourcetable;
}

bool IsRtcmStream(const NtripSourcetableStream& stream)
{
  return stream.format.has_value() &&
         ContainsCaseInsensitive(*stream.format, "RTCM");
}

bool RequiresNmea(const NtripSourcetableStream& stream)
{
  return stream.nmea_required.value_or(false);
}

bool SupportsMsm(const NtripSourcetableStream& stream)
{
  if (!IsRtcmStream(stream) || !stream.format_details.has_value())
  {
    return false;
  }

  return ContainsCaseInsensitive(*stream.format_details, "MSM") ||
         ContainsMsmMessageType(*stream.format_details);
}

const NtripSourcetableStream* FindMountpoint(const NtripSourcetable& sourcetable,
                                             const std::string_view mountpoint)
{
  const std::string_view normalized_mountpoint = NormalizeMountpointKey(mountpoint);
  for (const auto& stream : sourcetable.streams)
  {
    if (NormalizeMountpointKey(stream.mountpoint) == normalized_mountpoint)
    {
      return &stream;
    }
  }

  return nullptr;
}

std::vector<const NtripSourcetableStream*> FilterRtcmStreams(const NtripSourcetable& sourcetable)
{
  std::vector<const NtripSourcetableStream*> filtered;
  filtered.reserve(sourcetable.streams.size());
  for (const auto& stream : sourcetable.streams)
  {
    if (IsRtcmStream(stream))
    {
      filtered.push_back(&stream);
    }
  }

  return filtered;
}

}  // namespace universal_gnss_ntrip
