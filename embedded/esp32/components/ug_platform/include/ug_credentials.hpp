// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_https_diagnostics.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
namespace ug::embedded
{
bool Sha256(std::string_view, std::array<std::uint8_t, 32>&);
struct WebIdentity
{
  std::string certificate_pem, private_key_pem;
};
struct SecureIdentityDelete
{
  std::size_t size{};
  void operator()(std::uint8_t*) const;
};
// Checked allocation on the no-exceptions IDF build. The blob and PEM spans
// remain owned until HTTPS stops; destruction wipes private key material.
struct LoadedWebIdentity
{
  std::unique_ptr<std::uint8_t, SecureIdentityDelete> bytes{nullptr, {}};
  std::size_t certificate_size{}, private_key_size{};
  const char* certificate_pem() const
  {
    return bytes ? reinterpret_cast<const char*>(bytes.get() + 4) : nullptr;
  }
  const char* private_key_pem() const
  {
    return bytes ? reinterpret_cast<const char*>(bytes.get() + 5 + certificate_size) : nullptr;
  }
};
// Local-console provisioning only. Remote API never accepts/retrieves these keys.
bool StoreWebIdentity(const WebIdentity&, std::string& error);
HttpsStartupStatus LoadWebIdentity(LoadedWebIdentity&);
// TLS endpoint paths may reference nvs://ug_tls/<name>; no filesystem mount required.
bool StoreTlsObject(std::string_view name,
                    std::string_view pem,
                    bool private_key,
                    std::string& error);
bool ApplyTlsObject(std::string_view body, std::string& error);
bool DeleteTlsObject(std::string_view name, std::string& error);
}  // namespace ug::embedded
