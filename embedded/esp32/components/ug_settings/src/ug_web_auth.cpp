// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_web_auth.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
namespace ug::embedded
{
void Wipe(void* bytes, std::size_t size)
{
  auto* p = static_cast<volatile std::uint8_t*>(bytes);
  while (size--)
    *p++ = 0;
}
namespace
{
bool Equal(std::string_view a, std::string_view b)
{
  if (a.size() != b.size())
    return false;
  volatile unsigned difference = 0;
  for (std::size_t i = 0; i < a.size(); ++i)
    difference = difference | (a[i] ^ b[i]);
  return difference == 0;
}
}  // namespace
void WebAuth::Start(bool fresh, std::string_view legacy)
{
  sessions_ = {};
  record_ = {};
  legacy_ = {};
  legacy_buffer_ = {};
  next_login_ = -1;
  const auto state = backend_.Read(record_);
  configured_ = state == AuthRead::kFound;
  fresh_ = state == AuthRead::kMissing && fresh && legacy.empty();
  ready_ = state != AuthRead::kError && (configured_ || fresh_ || legacy.size() == 64);
  if (ready_ && !configured_ && legacy.size() == 64)
  {
    std::copy(legacy.begin(), legacy.end(), legacy_buffer_.begin());
    legacy_ = {legacy_buffer_.data(), 64};
  }
}
bool WebAuth::Verify(std::string_view password)
{
  if (!ready_ || password.empty() || password.size() > 128)
    return false;
  if (!configured_)
    return fresh_ ? Equal(password, "admin") : backend_.Legacy(password, legacy_);
  std::array<std::uint8_t, 32> hash{};
  const bool derived = backend_.Derive(password, record_, hash);
  const bool match =
      derived && Equal({reinterpret_cast<const char*>(hash.data()), hash.size()},
                       {reinterpret_cast<const char*>(record_.hash.data()), record_.hash.size()});
  Wipe(hash.data(), hash.size());
  return match;
}
WebAuth::Session* WebAuth::Find(std::string_view token, std::int64_t now)
{
  if (!ready_ || now < 0 || token.size() != 64)
    return nullptr;
  for (auto& s : sessions_)
  {
    if (s.active && (now < s.used || now - s.created >= kLifetime || now - s.used >= kIdle))
    {
      Wipe(s.token.data(), s.token.size());
      s.active = false;
    }
    if (s.active && Equal(token, {s.token.data(), 64}))
      return &s;
  }
  return nullptr;
}
bool WebAuth::Login(std::string_view user,
                    std::string_view password,
                    std::int64_t now,
                    std::array<char, 65>& token,
                    bool& restricted)
{
  token = {};
  restricted = true;
  if (!ready_ || now < 0 || now < next_login_ ||
      now > std::numeric_limits<std::int64_t>::max() - 1000000)
    return false;
  next_login_ = now + 1000000;  // Bound expensive password work; shared local limiter.
  if (user != "admin" || !Verify(password))
    return false;
  Find(std::string_view(token.data(), 64),
       now);  // Expire old slots; no eviction of valid sessions.
  for (auto& s : sessions_)
    if (!s.active)
    {
      std::array<std::uint8_t, 32> random{};
      if (!backend_.Random(random.data(), random.size()))
        return false;
      constexpr char digits[] = "0123456789abcdef";
      for (unsigned i = 0; i < random.size(); ++i)
      {
        token[2 * i] = digits[random[i] >> 4];
        token[2 * i + 1] = digits[random[i] & 15];
      }
      Wipe(random.data(), random.size());
      s.token = token;
      s.used = s.created = now;
      s.active = true;
      s.restricted = !configured_;
      restricted = s.restricted;
      return true;
    }
  return false;
}
bool WebAuth::Authorized(std::string_view token, std::int64_t now, bool restricted)
{
  auto* s = Find(token, now);
  if (!s || (s->restricted && !restricted))
    return false;
  s->used = now;
  return true;
}
bool WebAuth::Change(std::string_view token,
                     std::string_view current,
                     std::string_view next,
                     std::int64_t now)
{
  if (!Authorized(token, now, true) || next.size() < 8 || next.size() > 128 || next == current ||
      !Verify(current))
    return false;
  PasswordRecord candidate{};
  if (!backend_.Random(candidate.salt.data(), candidate.salt.size()) ||
      !backend_.Derive(next, candidate, candidate.hash))
  {
    Wipe(&candidate, sizeof(candidate));
    return false;
  }
  if (!backend_.Save(candidate))
  {
    ready_ = false;
    sessions_ = {};
    Wipe(&candidate, sizeof(candidate));
    return false;
  }
  record_ = candidate;
  Wipe(&candidate, sizeof(candidate));
  configured_ = true;
  fresh_ = false;
  legacy_ = {};
  legacy_buffer_ = {};
  sessions_ = {};
  return true;
}
void WebAuth::Logout(std::string_view token)
{
  for (auto& s : sessions_)
    if (s.active && Equal(token, {s.token.data(), 64}))
    {
      Wipe(s.token.data(), s.token.size());
      s.active = false;
    }
}
}  // namespace ug::embedded
