// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace ug::embedded
{
using Sha256Function = bool (*)(std::string_view, std::array<std::uint8_t, 32>&);
bool AuthorizedBearer(std::string_view header,
                      std::string_view expected_sha256,
                      Sha256Function digest);
}  // namespace ug::embedded
