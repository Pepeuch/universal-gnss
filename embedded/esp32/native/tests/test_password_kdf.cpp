// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_password_kdf.hpp"
#include <cassert>
#include <cstring>
#include <openssl/evp.h>
#include <openssl/hmac.h>
using namespace ug::embedded;
static unsigned calls{}, yields{};
static bool failing{};
static bool
mac(void*, const std::uint8_t* data, std::size_t size, std::array<std::uint8_t, 32>& out)
{
  ++calls;
  if (failing && calls == 2)
    return false;
  unsigned n{};
  return HMAC(EVP_sha256(), "password", 8, data, size, out.data(), &n) && n == 32;
}
int main()
{
  PasswordRecord record{};
  for (unsigned i = 0; i < 16; ++i)
    record.salt[i] = i;
  std::array<std::uint8_t, 32> actual{}, expected{};
  assert(PasswordKdf(record, WebAuth::kIterations, mac, nullptr, [] { ++yields; }, actual));
  assert(PKCS5_PBKDF2_HMAC("password",
                           8,
                           record.salt.data(),
                           16,
                           WebAuth::kIterations,
                           EVP_sha256(),
                           32,
                           expected.data()) == 1);
  assert(actual == expected && calls == WebAuth::kIterations && yields == 585);
  calls = 0;
  failing = true;
  actual.fill(9);
  assert(!PasswordKdf(record, 4, mac, nullptr, nullptr, actual) &&
         (actual == std::array<std::uint8_t, 32>{}));
  assert(!PasswordKdf(record, 0, mac, nullptr, nullptr, actual));
}
