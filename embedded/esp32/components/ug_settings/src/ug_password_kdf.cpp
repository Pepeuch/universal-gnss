// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_password_kdf.hpp"
#include <algorithm>
namespace ug::embedded
{
bool PasswordKdf(const PasswordRecord& record,
                 unsigned iterations,
                 PasswordMac mac,
                 void* context,
                 void (*yield)(),
                 std::array<std::uint8_t, 32>& out)
{
  out = {};
  if (!mac || !iterations)
    return false;
  std::array<std::uint8_t, 20> salt{};
  std::copy(record.salt.begin(), record.salt.end(), salt.begin());
  salt.back() = 1;
  std::array<std::uint8_t, 32> u{}, next{};
  bool ok = mac(context, salt.data(), salt.size(), u);
  out = u;
  for (unsigned i = 1; ok && i < iterations; ++i)
  {
    ok = mac(context, u.data(), u.size(), next);
    if (ok)
      for (unsigned j = 0; j < out.size(); ++j)
        out[j] ^= next[j];
    u = next;
    if (yield && (i & 1023u) == 0)
      yield();
  }
  Wipe(u.data(), u.size());
  Wipe(next.data(), next.size());
  Wipe(salt.data(), salt.size());
  if (!ok)
    Wipe(out.data(), out.size());
  return ok;
}
}  // namespace ug::embedded
