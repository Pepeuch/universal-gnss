// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_web_auth.hpp"
namespace ug::embedded
{
// S3 startup only, before ADC/Wi-Fi/BT/peripheral tasks. Never called on demand.
bool InitializeWebSessionRandom();
WebAuth& WebAuthentication();
// Read-only fixed-vector diagnostic, never reads/writes an operator password.
bool BenchmarkWebPassword(std::int64_t& elapsed_us);
}  // namespace ug::embedded
