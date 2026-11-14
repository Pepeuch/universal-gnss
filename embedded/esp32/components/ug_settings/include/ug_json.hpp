// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "cJSON.h"
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
namespace ug::embedded
{
using JsonDocument = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
// Shared 8KiB / depth12 / node512 budget, complete document and duplicate-key check.
JsonDocument ParseBoundedObject(std::string_view json, std::string& error);
bool OnlyKeys(const cJSON*, std::initializer_list<const char*> keys);
}  // namespace ug::embedded
