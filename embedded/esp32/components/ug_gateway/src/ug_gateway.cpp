// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_gateway.hpp"
#include "ug_rtcm.h"
#include <algorithm>

namespace ug::embedded
{
void Gateway::Invalidate(const std::size_t source)
{
  sources_[source].framer.Reset();
  for (auto& sink : sinks_)
  {
    // Compact in place; pending frames from other sources keep their order.
    std::size_t retained = 0;
    const auto count = sink.count;
    for (std::size_t i = 0; i < count; ++i)
    {
      const auto from = (sink.head + i) % kDepth;
      if (sink.queue[from].source == source)
      {
        ++metrics_.invalidated;
        continue;
      }
      const auto to = (sink.head + retained++) % kDepth;
      if (from != to)
        sink.queue[to] = sink.queue[from];
    }
    sink.count = retained;
  }
}

bool Gateway::ConfigureSource(const std::size_t source,
                              const std::uint64_t incarnation,
                              const bool enabled)
{
  if (source >= kSources || incarnation == 0 || incarnation <= sources_[source].incarnation)
    return false;
  auto& state = sources_[source];
  // Reconfiguration is an explicit invalidation, even if numeric settings match.
  Invalidate(source);
  state.incarnation = incarnation;
  state.enabled = enabled;
  state.last_receipt_ns = 0;
  return true;
}

bool Gateway::ConfigureSink(const std::size_t sink,
                            const bool enabled,
                            const std::int64_t max_age_ns)
{
  if (sink >= kSinks || max_age_ns <= 0)
    return false;
  auto& state = sinks_[sink];
  state.head = state.count = 0;
  state.enabled = enabled;
  state.max_age_ns = max_age_ns;
  return true;
}

bool Gateway::Feed(const std::size_t source,
                   const std::uint64_t incarnation,
                   const std::uint8_t* bytes,
                   const std::size_t size,
                   const std::int64_t receipt_ns)
{
  if (source >= kSources || (!bytes && size) || size > 4096 || receipt_ns < 0)
    return false;
  auto& state = sources_[source];
  if (!state.enabled || incarnation != state.incarnation || receipt_ns < state.last_receipt_ns)
    return false;
  state.last_receipt_ns = receipt_ns;
  for (std::size_t i = 0; i < size; ++i)
  {
    const auto result = state.framer.PushByte(bytes[i], receipt_ns);
    if (result.status != universal_gnss_protocols::ParserStatus::kRecordReady || !result.record)
      continue;
    const auto& record = *result.record;
    // RecordReady is not a checksum guarantee.
    Route(source,
          incarnation,
          record.raw_bytes.data(),
          record.raw_bytes.size(),
          record.timestamp_ns.value_or(receipt_ns));
  }
  return true;
}

bool Gateway::FeedFrame(const std::size_t source,
                        const std::uint64_t incarnation,
                        const std::uint8_t* bytes,
                        const std::size_t size,
                        const std::int64_t receipt_ns)
{
  if (source >= kSources || receipt_ns < 0)
    return false;
  auto& state = sources_[source];
  if (!state.enabled || incarnation != state.incarnation || receipt_ns < state.last_receipt_ns)
    return false;
  state.last_receipt_ns = receipt_ns;
  return Route(source, incarnation, bytes, size, receipt_ns);
}

bool Gateway::Route(const std::size_t source,
                    const std::uint64_t incarnation,
                    const std::uint8_t* bytes,
                    const std::size_t size,
                    const std::int64_t receipt_ns)
{
  if (!ug_rtcm_frame_valid(bytes, size))
  {
    ++metrics_.rejected;
    return false;
  }
  ++metrics_.valid;
  Frame frame;
  frame.size = size;
  frame.source = source;
  frame.incarnation = incarnation;
  frame.receipt_ns = receipt_ns;
  std::copy(bytes, bytes + size, frame.bytes.begin());
  for (auto& sink : sinks_)
  {
    if (!sink.enabled)
      continue;
    if (sink.count == kDepth)
    {
      ++metrics_.dropped_full;
      continue;
    }
    sink.queue[(sink.head + sink.count++) % kDepth] = frame;
  }
  return true;
}

bool Gateway::Pop(const std::size_t sink, const std::int64_t now_ns, Frame& out)
{
  if (sink >= kSinks || now_ns < 0)
    return false;
  auto& state = sinks_[sink];
  if (!state.enabled)
    return false;
  while (state.count)
  {
    const auto& frame = state.queue[state.head];
    state.head = (state.head + 1) % kDepth;
    --state.count;
    if (now_ns < frame.receipt_ns || now_ns - frame.receipt_ns > state.max_age_ns)
    {
      ++metrics_.dropped_stale;
      continue;
    }
    out = frame;
    return true;
  }
  return false;
}
}  // namespace ug::embedded
