// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_pairing_pmk.h"
#include <array>
#include <cassert>
#include <cstring>
extern bool pmk_hash_missing, pmk_hash_failed;
extern bool pmk_import_failed, pmk_destroy_failed;
extern unsigned pmk_imports, pmk_destroys;
int main()
{
  std::array<unsigned char, 16> key{}, pmk{};
  for (unsigned i = 0; i < 16; ++i)
    key[i] = i;
  const auto original = key;
  constexpr unsigned char expected[] = {0x7f,
                                        0x8d,
                                        0x3f,
                                        0x26,
                                        0xd8,
                                        0x5c,
                                        0xe3,
                                        0x09,
                                        0x5b,
                                        0x7a,
                                        0x3c,
                                        0x61,
                                        0x79,
                                        0x77,
                                        0x90,
                                        0x6b};
  assert(ug_pairing_derive_pmk(key.data(), pmk.data()));
  assert(!std::memcmp(pmk.data(), expected, 16) && key == original && key != pmk);
  std::array<unsigned char, 16> same{};
  assert(ug_pairing_derive_pmk(key.data(), same.data()) && same == pmk);
  key[0] ^= 1;
  assert(ug_pairing_derive_pmk(key.data(), same.data()) && same != pmk);
  for (bool missing : {false, true})
  {
    pmk_hash_missing = missing;
    pmk_hash_failed = !missing;
    pmk.fill(0xaa);
    assert(!ug_pairing_derive_pmk(key.data(), pmk.data()));
    for (auto byte : pmk)
      assert(!byte);
  }
  pmk_hash_missing = pmk_hash_failed = false;
  pmk_import_failed = true;
  assert(!ug_pairing_derive_pmk(key.data(), pmk.data()));
  pmk_import_failed = false;
  pmk_destroy_failed = true;
  assert(!ug_pairing_derive_pmk(key.data(), pmk.data()));
  for (auto byte : pmk)
    assert(!byte);
  assert(pmk_imports == pmk_destroys);
  assert(!ug_pairing_derive_pmk(nullptr, pmk.data()));
  assert(!ug_pairing_derive_pmk(key.data(), nullptr));
}
