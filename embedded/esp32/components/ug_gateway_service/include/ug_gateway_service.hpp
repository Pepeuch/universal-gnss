// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
#include "ug_gateway.hpp"
namespace ug::embedded
{
// Callback must only enqueue/copy a complete frame; never perform socket/UART I/O.
using FrameDestination = bool (*)(const Gateway::Frame&, void*);
esp_err_t StartGatewayService();
std::uint64_t BeginCorrectionSource(std::size_t source, bool enabled);
bool CorrectionSourceCurrent(std::size_t source, std::uint64_t incarnation);
bool SubmitCorrectionBytes(std::size_t source,
                           std::uint64_t incarnation,
                           const std::uint8_t* bytes,
                           std::size_t size,
                           std::int64_t receipt_ns,
                           bool complete_frame = false);
bool SetCorrectionDestination(std::size_t sink, FrameDestination callback, void* context);
struct GatewayServiceStats
{
  Gateway::Metrics routing{};
  std::uint64_t ingress_dropped{0}, destination_dropped{0};
};
GatewayServiceStats GatewayStats();
}  // namespace ug::embedded
