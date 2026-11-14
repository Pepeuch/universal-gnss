// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_settings.hpp"
namespace ug::embedded
{
bool ExportPortableSettings(const Settings&, const Capabilities&, std::string&, std::string& error);
// Pure preview: never enables services, sends receiver commands or changes identity/keys.
bool ImportPortableSettings(
    std::string_view, const Settings&, const Capabilities&, Settings&, std::string& error);
}  // namespace ug::embedded
