// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_web_auth.hpp"
#include <algorithm>
#include <array>
#include <string_view>

namespace ug::embedded
{
// HMAC-SHA256 with the two keyed prefix states prepared once per derivation.
// Hash supplies Start/Update/Clone/Finish/Reset, including cleanup on failure.
// No password/prefix cache survives this object; no hash implementation lives here.
template <class Hash>
class PasswordHmac
{
public:
  PasswordHmac() = default;
  PasswordHmac(const PasswordHmac&) = delete;
  PasswordHmac& operator=(const PasswordHmac&) = delete;
  ~PasswordHmac()
  {
    Reset();
  }

  bool Initialize(std::string_view password)
  {
    Reset();
    std::array<std::uint8_t, 64> pad{};
    std::array<std::uint8_t, 32> digest{};
    const auto* key = reinterpret_cast<const std::uint8_t*>(password.data());
    auto size = password.size();
    bool ok = true;
    if (size > pad.size())
    {
      ok = work_.Start() && work_.Update(key, size) && work_.Finish(digest);
      key = digest.data();
      size = digest.size();
    }
    if (ok)
    {
      std::fill(pad.begin(), pad.end(), 0x36);
      for (std::size_t i = 0; i < size; ++i)
        pad[i] ^= key[i];
      ok = inner_.Start() && inner_.Update(pad.data(), pad.size());
      for (auto& byte : pad)
        byte ^= 0x36 ^ 0x5c;
      ok = ok && outer_.Start() && outer_.Update(pad.data(), pad.size());
    }
    Wipe(pad.data(), pad.size());
    Wipe(digest.data(), digest.size());
    work_.Reset();
    ready_ = ok;
    if (!ok)
      Reset();
    return ok;
  }

  bool Compute(const std::uint8_t* data, std::size_t size, std::array<std::uint8_t, 32>& out)
  {
    std::array<std::uint8_t, 32> digest{};
    out = {};
    work_.Reset();
    bool ok = ready_ && (data || !size) && work_.Clone(inner_) && work_.Update(data, size) &&
              work_.Finish(digest);
    work_.Reset();
    ok = ok && work_.Clone(outer_) && work_.Update(digest.data(), digest.size()) &&
         work_.Finish(out);
    Wipe(digest.data(), digest.size());
    work_.Reset();
    if (!ok)
    {
      Wipe(out.data(), out.size());
      Reset();
    }
    return ok;
  }

private:
  void Reset()
  {
    ready_ = false;
    work_.Reset();
    inner_.Reset();
    outer_.Reset();
  }
  Hash inner_{}, outer_{}, work_{};
  bool ready_{};
};
}  // namespace ug::embedded
