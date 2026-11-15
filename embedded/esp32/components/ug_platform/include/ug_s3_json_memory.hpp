// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstdint>
namespace ug::embedded
{
// Once at S3 startup, before any JSON document or application worker exists.
// All cJSON-owned buffers then use PSRAM; no fallback to internal RAM.
void InitializeS3JsonMemory();
std::uint32_t S3JsonMemoryFailures();
std::uint32_t S3JsonInjectedFailures();
bool ArmS3JsonMemoryFailure(int allocations_before_one_failure);
}  // namespace ug::embedded
