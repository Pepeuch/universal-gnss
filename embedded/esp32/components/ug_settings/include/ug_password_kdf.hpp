// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_web_auth.hpp"
namespace ug::embedded
{
using PasswordMac = bool (*)(void*,
                             const std::uint8_t*,
                             std::size_t,
                             std::array<std::uint8_t, 32>&);
// PBKDF2-HMAC-SHA256, 32-byte result; the supplied MAC is keyed with the password.
bool PasswordKdf(const PasswordRecord&,
                 unsigned iterations,
                 PasswordMac,
                 void* context,
                 void (*yield)(),
                 std::array<std::uint8_t, 32>& out);
}  // namespace ug::embedded
