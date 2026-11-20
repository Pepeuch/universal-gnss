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
// Task-only; never call from the routing owner or a destination callback.
// Caller serializes publication for this sink and must not republish the retiring
// context. True means all old callbacks finished; false requires retaining it.
bool RetireCorrectionDestination(std::size_t sink, std::uint32_t timeout_ms);
// Nonblocking form for a cooperative owner. Retain the context until this exact
// ticket is acknowledged; do not publish a fresh ticket on every poll.
std::uint64_t WithdrawCorrectionDestination(std::size_t sink);
bool CorrectionDestinationRetired(std::size_t sink, std::uint64_t ticket);
struct GatewayServiceStats
{
  Gateway::Metrics routing{};
  std::uint64_t ingress_dropped{0}, destination_dropped{0};
};
GatewayServiceStats GatewayStats();
}  // namespace ug::embedded
