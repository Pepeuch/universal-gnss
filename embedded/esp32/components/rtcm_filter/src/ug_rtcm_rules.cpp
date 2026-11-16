// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_rtcm_rules.hpp"
#include <charconv>
namespace ug::embedded
{
bool RtcmRules::Configure(std::string_view text, std::string& error)
{
  RtcmRules next;
  error.clear();
  const auto fail = [&](const char* why) {
    error = why;
    return false;
  };
  if (text.size() > 512)
    return fail("RTCM list exceeds 512 characters");
  auto trim = [](std::string_view value) {
    while (!value.empty() && value.front() == ' ')
      value.remove_prefix(1);
    while (!value.empty() && value.back() == ' ')
      value.remove_suffix(1);
    return value;
  };
  text = trim(text);
  while (!text.empty())
  {
    const auto comma = text.find(',');
    auto item = trim(text.substr(0, comma));
    if (item.empty() || next.count_ == kMaxRules)
      return fail("RTCM list requires 1..32 nonempty, distinct message types");
    const auto open = item.find('(');
    auto number = item.substr(0, open);
    unsigned type{}, seconds{};
    auto parse = [](std::string_view value, unsigned& out) {
      const auto r = std::from_chars(value.data(), value.data() + value.size(), out);
      return !value.empty() && r.ec == std::errc{} && r.ptr == value.data() + value.size();
    };
    if (!parse(number, type) || type < 1 || type > 4095)
      return fail("RTCM message type must be an integer between 1 and 4095");
    if (open != std::string_view::npos &&
        (item.back() != ')' || !parse(item.substr(open + 1, item.size() - open - 2), seconds) ||
         seconds > 86400))
      return fail("RTCM interval must be integer seconds between 0 and 86400");
    for (unsigned i = 0; i < next.count_; ++i)
      if (next.rules_[i].type == type)
        return fail("Duplicate RTCM message type");
    next.rules_[next.count_++] = {-1, seconds, static_cast<std::uint16_t>(type)};
    if (comma == std::string_view::npos)
      break;
    text.remove_prefix(comma + 1);
    if (trim(text).empty())
      return fail("Empty RTCM message after comma");
  }
  *this = next;
  return true;
}
bool RtcmRules::Allows(std::uint16_t type, std::int64_t now) const
{
  if (now < 0)
    return false;
  if (!count_)
    return true;  // Empty list preserves the unfiltered historical stream.
  for (unsigned i = 0; i < count_; ++i)
  {
    const auto& r = rules_[i];
    if (r.type == type)
      return r.last < 0 ||
             (now >= r.last && now - r.last >= std::int64_t(r.seconds) * 1000000000LL);
  }
  return false;
}
void RtcmRules::Sent(std::uint16_t type, std::int64_t now)
{
  for (unsigned i = 0; i < count_; ++i)
    if (rules_[i].type == type)
      rules_[i].last = now;
}
}  // namespace ug::embedded
