// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_pairing.h"
#include "ug_pairing_window.hpp"
#include <array>
#include <string_view>
namespace ug::embedded
{
// Owned by the local USB console task. Discovery only: this state never grants
// network authorization or writes pairing/configuration. Times are monotonic us.
class GatewayPairingRead
{
public:
  bool Query(std::string_view command, std::int64_t now_us)
  {
    if (command == "UGCFG/1 GET")
      return window_.Active(now_us);
    std::array<char, 17> nonce{};
    if (!ug_pairing_open(command.data(), command.size(), nonce.data()) || !window_.Open(now_us))
      return false;
    nonce_ = nonce;
    return true;
  }
  const char* nonce() const
  {
    return nonce_.data();
  }

private:
  PairingWindow window_;
  std::array<char, 17> nonce_{};
};
}  // namespace ug::embedded
