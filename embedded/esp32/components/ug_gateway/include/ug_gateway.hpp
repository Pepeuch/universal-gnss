// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

#ifndef UG_GATEWAY_SINKS
#define UG_GATEWAY_SINKS 8
#endif
static_assert(UG_GATEWAY_SINKS >= 2 && UG_GATEWAY_SINKS <= 8);

namespace ug::embedded
{
// Single-owner engine. UART/network tasks enqueue input to this owner; sinks
// consume whole frames without retaining a borrowed pointer. No callback/ISR use.
class Gateway
{
public:
  static constexpr std::size_t kSources = 6;  // local receiver + five network slots
  // Native capacity only; destination identity/protocol is unchanged.
  static constexpr std::size_t kSinks = UG_GATEWAY_SINKS;
  static constexpr std::size_t kDepth = 2;
  static constexpr std::size_t kMaxFrame = 1029;
  struct Frame
  {
    std::array<std::uint8_t, kMaxFrame> bytes{};
    std::size_t size{0};
    std::size_t source{0};
    std::uint64_t incarnation{0};
    std::int64_t receipt_ns{0};  // IDF monotonic receipt domain, never GNSS UTC.
  };
  struct Metrics
  {
    std::uint64_t valid{0}, rejected{0}, dropped_full{0}, dropped_stale{0}, invalidated{0};
  };
  bool ConfigureSource(std::size_t source, std::uint64_t incarnation, bool enabled);
  bool ConfigureSink(std::size_t sink, bool enabled, std::int64_t max_age_ns);
  bool Feed(std::size_t source,
            std::uint64_t incarnation,
            const std::uint8_t* bytes,
            std::size_t size,
            std::int64_t receipt_ns);
  // For an already framed producer (e.g. UG NtripClient observed frames).
  bool FeedFrame(std::size_t source,
                 std::uint64_t incarnation,
                 const std::uint8_t* bytes,
                 std::size_t size,
                 std::int64_t receipt_ns);
  bool Pop(std::size_t sink, std::int64_t now_ns, Frame& out);
  const Metrics& metrics() const
  {
    return metrics_;
  }

private:
  struct Source
  {
    universal_gnss_protocols::RtcmFrameFramer framer{};
    std::uint64_t incarnation{0};
    std::int64_t last_receipt_ns{0};
    bool enabled{false};
  };
  struct Sink
  {
    std::array<Frame, kDepth> queue{};
    std::size_t head{0}, count{0};
    std::int64_t max_age_ns{0};
    bool enabled{false};
  };
  void Invalidate(std::size_t source);
  bool Route(std::size_t source,
             std::uint64_t incarnation,
             const std::uint8_t* bytes,
             std::size_t size,
             std::int64_t receipt_ns);
  std::array<Source, kSources> sources_{};
  std::array<Sink, kSinks> sinks_{};
  Metrics metrics_{};
};
}  // namespace ug::embedded
