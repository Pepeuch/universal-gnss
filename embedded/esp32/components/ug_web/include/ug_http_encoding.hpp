// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <string_view>
namespace ug::embedded
{
inline std::string_view TrimHttpWhitespace(std::string_view text)
{
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    text.remove_suffix(1);
  return text;
}
inline bool HttpTokenEqual(std::string_view text, std::string_view lower)
{
  if (text.size() != lower.size())
    return false;
  for (std::size_t i = 0; i < text.size(); ++i)
    if ((text[i] >= 'A' && text[i] <= 'Z' ? text[i] + ('a' - 'A') : text[i]) != lower[i])
      return false;
  return true;
}
inline bool PositiveHttpQuality(std::string_view text)
{
  text = TrimHttpWhitespace(text);
  if (text.empty() || (text[0] != '0' && text[0] != '1'))
    return false;
  bool positive = text[0] == '1';
  if (text.size() == 1)
    return positive;
  if (text[1] != '.' || text.size() > 5)
    return false;
  for (std::size_t i = 2; i < text.size(); ++i)
  {
    if (text[i] < '0' || text[i] > '9' || (text[0] == '1' && text[i] != '0'))
      return false;
    positive = positive || text[i] != '0';
  }
  return positive;
}
inline bool AcceptsGzip(std::string_view value, bool header_present)
{
  if (!header_present)
    return true;  // RFC 9110: absent field expresses no coding preference.
  bool gzip_seen = false, gzip_allowed = true, star_seen = false, star_allowed = true;
  while (!value.empty())
  {
    const auto comma = value.find(',');
    const auto entry = TrimHttpWhitespace(value.substr(0, comma));
    const auto semicolon = entry.find(';');
    const auto coding = TrimHttpWhitespace(entry.substr(0, semicolon));
    bool allowed = true;
    if (semicolon != std::string_view::npos)
    {
      const auto quality = TrimHttpWhitespace(entry.substr(semicolon + 1));
      const auto equals = quality.find('=');
      allowed = equals != std::string_view::npos &&
                HttpTokenEqual(TrimHttpWhitespace(quality.substr(0, equals)), "q") &&
                PositiveHttpQuality(quality.substr(equals + 1));
    }
    if (HttpTokenEqual(coding, "gzip"))
    {
      gzip_seen = true;
      gzip_allowed = gzip_allowed && allowed;  // Conflicting duplicates fail closed.
    }
    else if (coding == "*")
    {
      star_seen = true;
      star_allowed = star_allowed && allowed;
    }
    if (comma == std::string_view::npos)
      break;
    value.remove_prefix(comma + 1);
  }
  return gzip_seen ? gzip_allowed : star_seen && star_allowed;
}
}  // namespace ug::embedded
