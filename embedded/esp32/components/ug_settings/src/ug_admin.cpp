// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_admin.hpp"
#include <algorithm>
namespace ug::embedded
{
bool AuthorizedBearer(std::string_view header, std::string_view expected, Sha256Function digest)
{
  if (expected.size() != 64 || header.size() < 7 + 32 || header.size() > 7 + 128 || !digest)
    return false;
  const auto scheme = header.substr(0, 7);
  constexpr char bearer[] = "bearer ";
  for (unsigned i = 0; i < 7; ++i)
    if ((scheme[i] >= 'A' && scheme[i] <= 'Z' ? scheme[i] + 32 : scheme[i]) != bearer[i])
      return false;
  const auto token = header.substr(7);
  if (!std::all_of(token.begin(), token.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
      }))
    return false;
  std::array<std::uint8_t, 32> actual{};
  if (!digest(token, actual))
    return false;
  constexpr char hex[] = "0123456789abcdef";
  volatile unsigned mismatch = 0;
  for (unsigned i = 0; i < 32; ++i)
  {
    mismatch = mismatch | (hex[actual[i] >> 4] ^ expected[2 * i]);
    mismatch = mismatch | (hex[actual[i] & 15] ^ expected[2 * i + 1]);
  }
  return mismatch == 0;
}
}  // namespace ug::embedded
