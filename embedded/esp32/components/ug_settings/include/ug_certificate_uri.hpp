// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <optional>
#include <string_view>
namespace ug::embedded
{
inline bool ValidCertificateName(std::string_view name)
{
  if (name.empty() || name.size() > 15)
    return false;
  for (unsigned char c : name)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '-'))
      return false;
  return true;
}
inline std::optional<std::string_view> NvsCertificateName(std::string_view uri)
{
  constexpr std::string_view prefix = "nvs://ug_tls/";
  if (uri.substr(0, prefix.size()) != prefix)
    return std::nullopt;
  const auto name = uri.substr(prefix.size());
  if (!ValidCertificateName(name))
    return std::nullopt;
  return name;
}
}  // namespace ug::embedded
