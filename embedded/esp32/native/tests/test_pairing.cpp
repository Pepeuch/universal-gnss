// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_pairing.h"
#include "ug_pairing_window.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <string>
int main()
{
  constexpr auto key = "0123456789abcdef0123456789abcdef";
  ug_pairing_t p{}, restored{};
  assert(ug_pairing_from_text("02:00:00:00:00:02", key, 1, &p));
  std::array<std::uint8_t, UG_PAIRING_RECORD_SIZE> record{};
  assert(ug_pairing_encode(&p, record.data()) &&
         ug_pairing_decode(record.data(), record.size(), &restored));
  assert(restored.paired && restored.channel == 1 && !memcmp(p.peer, restored.peer, 6) &&
         !memcmp(p.key, restored.key, 16));
  for (std::size_t i = 0; i < record.size(); ++i)
  {
    auto corrupt = record;
    corrupt[i] ^= 1;
    restored.channel = 99;
    assert(!ug_pairing_decode(corrupt.data(), corrupt.size(), &restored) && restored.channel == 99);
  }
  for (auto peer : {"ff:ff:ff:ff:ff:ff",
                    "00:00:00:00:00:00",
                    "03:00:00:00:00:02",
                    "2:0:0:0:0:2",
                    "02:00:00:00:00:02x"})
    assert(!ug_pairing_from_text(peer, key, 1, &restored));
  for (auto secret : {"", "00000000000000000000000000000000", "0123456789abcdef0123456789abcdefx"})
    assert(!ug_pairing_from_text("02:00:00:00:00:02", secret, 1, &restored));
  assert(!ug_pairing_from_text("02:00:00:00:00:02", key, 0, &restored));
  assert(!ug_pairing_from_text("02:00:00:00:00:02", key, 12, &restored));
  ug_pairing_t empty{};
  assert(ug_pairing_encode(&empty, record.data()) &&
         ug_pairing_decode(record.data(), record.size(), &restored) && !restored.paired);
  ug_pair_command_t command{};
  for (auto text :
       {"UGCFG/1 GET",
        "UGCFG/1 CLEAR CONFIRM_UNPAIR",
        "UGCFG/1 RUN CONFIRM_RTCM",
        "UGCFG/1 SET 02:00:00:00:00:02 1 0123456789abcdef0123456789abcdef CONFIRM_PAIR"})
    assert(ug_pairing_command(text, strlen(text), &command));
  for (auto text :
       {"UGCFG/1 OPEN",
        "UGCFG/1 RUN",
        "UGCFG/1 CLEAR",
        "UGCFG/2 GET",
        "UGCFG/1 GET extra",
        "UGCFG/1 SET 02:00:00:00:00:02 01 0123456789abcdef0123456789abcdef CONFIRM_PAIR",
        "UGCFG/1 SET 02:00:00:00:00:02 1 0123456789abcdef0123456789abcdef CONFIRM_PAIRx",
        "UGCFG/1  SET 02:00:00:00:00:02 1 0123456789abcdef0123456789abcdef CONFIRM_PAIR"})
    assert(!ug_pairing_command(text, strlen(text), &command));
  // Bounded arbitrary command corpus; no input may modify output on rejection.
  std::uint32_t seed = 0x50414952;
  for (unsigned n = 0; n < 10000; ++n)
  {
    std::string text(n % 140, ' ');
    for (auto& c : text)
    {
      seed = seed * 1664525 + 1013904223;
      c = seed >> 24;
    }
    command.kind = UG_PAIR_RUN;
    const bool ok = ug_pairing_command(text.data(), text.size(), &command);
    assert(ok || command.kind == UG_PAIR_RUN);
  }
  const std::uint8_t local[6] = {2, 0, 0, 0, 0, 1}, other[6] = {2, 0, 0, 0, 0, 9},
                     broadcast[6] = {255, 255, 255, 255, 255, 255};
  assert(ug_pairing_sender(p.peer, p.peer, local, local));
  assert(!ug_pairing_sender(p.peer, other, local, local));
  assert(!ug_pairing_sender(p.peer, p.peer, broadcast, local));
  assert(!ug_pairing_sender(p.peer, p.peer, other, local));
  assert(!ug_pairing_sender(p.peer, nullptr, local, local));
  char nonce[17];
  assert(ug_pairing_open("UGCFG/1 OPEN 0123456789abcdef", 29, nonce) &&
         std::string(nonce) == "0123456789abcdef");
  assert(!ug_pairing_open("UGCFG/1 OPEN", 12, nonce));
  ug::embedded::PairingWindow window;
  assert(!window.Active(0) && !window.Open(-1));
  assert(window.Open(1000) && window.Active(1000) && !window.Active(999));
  assert(window.Active(120000999) && !window.Active(120001000));
  assert(!window.Open(INT64_MAX));
  window.Close();
  assert(!window.Active(1000));
}
