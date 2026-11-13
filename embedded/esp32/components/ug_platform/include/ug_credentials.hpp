// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
namespace ug::embedded
{
bool Sha256(std::string_view, std::array<std::uint8_t, 32>&);
struct WebIdentity
{
  std::string certificate_pem, private_key_pem;
};
// Local-console provisioning only. Remote API never accepts/retrieves these keys.
bool StoreWebIdentity(const WebIdentity&, std::string& error);
bool LoadWebIdentity(WebIdentity&, std::string& error);
}  // namespace ug::embedded
