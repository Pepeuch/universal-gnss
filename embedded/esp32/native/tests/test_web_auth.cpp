// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_web_auth.hpp"
#include <cassert>
#include <cstring>
#include <string>
using namespace ug::embedded;
class Backend : public AuthBackend
{
public:
  AuthRead state = AuthRead::kMissing;
  PasswordRecord record{};
  bool fail_random{}, fail_save{}, fail_derive{};
  unsigned generated = 0, saves = 0;
  AuthRead Read(PasswordRecord& out) override
  {
    out = record;
    return state;
  }
  bool Save(const PasswordRecord& value) override
  {
    ++saves;
    if (fail_save)
      return false;
    record = value;
    state = AuthRead::kFound;
    return true;
  }
  bool Random(std::uint8_t* bytes, std::size_t n) override
  {
    if (fail_random)
      return false;
    std::memset(bytes, ++generated, n);
    return true;
  }
  bool Derive(std::string_view password,
              const PasswordRecord& r,
              std::array<std::uint8_t, 32>& hash) override
  {
    // Lifecycle fixture, deliberately not cryptographic: derivation vectors tested separately.
    if (fail_derive)
      return false;
    hash = {};
    for (std::size_t i = 0; i < password.size(); ++i)
      hash[i % 32] ^= password[i];
    for (unsigned i = 0; i < 32; ++i)
      hash[i] ^= r.salt[i % 16];
    return true;
  }
  bool Legacy(std::string_view value, std::string_view expected) override
  {
    return value == "old-private-token" && expected == std::string(64, 'a');
  }
};
int main()
{
  Backend b;
  WebAuth a(b);
  std::array<char, 65> t{}, second{};
  bool restricted;
  a.Start(true, {});
  assert(!a.Login("other", "admin", 0, t, restricted));
  assert(!a.Login("admin", "admin", 1, t, restricted));  // limiter
  assert(a.Login("admin", "admin", 1000000, t, restricted) && restricted);
  std::string token = t.data();
  assert(!a.Authorized(token, 1000001));
  assert(a.Authorized(token, 1000001, true));
  assert(!a.Change(token, "admin", "short", 1000002));
  assert(!a.Change(token, "wrong", "a-private-password", 1000002));
  assert(a.Change(token, "admin", "a-private-password", 1000002));
  assert(!a.Authorized(token, 1000003, true));
  assert(!a.Login("admin", "admin", 2000000, t, restricted));
  assert(a.Login("admin", "a-private-password", 3000000, t, restricted) && !restricted);
  assert(a.Authorized(t.data(), 3000001));
  a.Logout(t.data());
  assert(!a.Authorized(t.data(), 3000002));
  a.Start(true, {});  // Stored identity wins over fresh flag; no reset.
  assert(!a.Login("admin", "admin", 0, t, restricted));
  assert(a.Login("admin", "a-private-password", 1000000, t, restricted));
  assert(!a.Authorized(t.data(), 1000000 + WebAuth::kIdle));
  assert(a.Login("admin", "a-private-password", 1000000000, t, restricted));
  assert(!a.Authorized(t.data(), 999999999));  // unrelated/backward clock invalidates
  a.Start(false, {});
  assert(a.Login("admin", "a-private-password", 0, t, restricted));
  for (int i = 1; i <= 3; ++i)
    assert(a.Authorized(t.data(), i * 500000000LL));
  assert(!a.Authorized(t.data(), WebAuth::kLifetime));
  b.state = AuthRead::kMissing;
  a.Start(false, {});
  assert(!a.Login("admin", "admin", 0, t, restricted));  // Existing non-fresh install stays locked.
  a.Start(false, std::string(64, 'a'));
  assert(a.LegacyAllowed() && !a.Login("admin", "admin", 0, t, restricted));
  assert(a.Login("admin", "old-private-token", 1000000, t, restricted) && restricted);
  assert(!a.Authorized(t.data(), 1000001));
  assert(a.Change(t.data(), "old-private-token", "new-migrated-password", 1000002));
  assert(!a.LegacyAllowed());
  a.Start(false, std::string(64, 'a'));
  assert(!a.LegacyAllowed());
  assert(!a.Login("admin", "old-private-token", 0, t, restricted));
  b.state = AuthRead::kError;
  a.Start(true, {});
  assert(!a.ready());
  assert(!a.Login("admin", "admin", 0, t, restricted));
  b.state = AuthRead::kMissing;
  a.Start(true, {});
  b.fail_random = true;
  assert(!a.Login("admin", "admin", 0, t, restricted));
  b.fail_random = false;
  assert(a.Login("admin", "admin", 1000000, t, restricted));
  b.fail_derive = true;
  assert(!a.Change(t.data(), "admin", "valid-private-password", 1000001));
  assert(b.saves == 2);
  b.fail_derive = false;
  b.fail_save = true;
  assert(!a.Change(t.data(), "admin", "valid-private-password", 1000002) && !a.ready());
  assert(!a.Authorized(t.data(), 1000003, true));
  b.fail_save = false;
  b.state = AuthRead::kMissing;
  a.Start(true, {});
  for (int i = 0; i < 4; ++i)
    assert(a.Login("admin", "admin", i * 1000000LL, t, restricted));
  assert(!a.Login("admin", "admin", 4000000, t, restricted));  // bounded slots, no eviction
  b.state = AuthRead::kMissing;
  a.Start(true, {});
  assert(a.Login("admin", "admin", 0, t, restricted));
  assert(!a.Change(t.data(), "admin", "seven77", 1));
  assert(a.Change(t.data(), "admin", "eight888", 1));
  assert(a.Login("admin", "eight888", 1000000, t, restricted) && !restricted);
}
