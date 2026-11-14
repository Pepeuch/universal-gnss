// SPDX-License-Identifier: LGPL-3.0-only
#include "espnow_rtcm_output.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>
struct Writer
{
  std::vector<std::uint8_t> bytes;
  std::uint32_t now{}, max_wait{};
  unsigned calls{}, mode{};
  bool frozen{};
};
static std::uint32_t Clock(void* context)
{
  auto& state = *static_cast<Writer*>(context);
  if (!state.frozen)
    ++state.now;
  return state.now;
}
static void Idle(void*)
{
}
static int Write(const std::uint8_t* bytes, std::size_t size, std::uint32_t wait, void* context)
{
  auto& state = *static_cast<Writer*>(context);
  assert(wait > 0 && wait <= 20);
  state.max_wait = std::max(state.max_wait, wait);
  ++state.calls;
  if (state.mode == 1 || (state.mode == 2 && state.calls > 1))
    return 0;
  if (state.mode == 3)
    return -1;
  if (state.mode == 4)
    return static_cast<int>(size + 1);
  const auto count = std::min<std::size_t>(size, 48);
  state.bytes.insert(state.bytes.end(), bytes, bytes + count);
  return count;
}
int main()
{
  std::array<std::uint8_t, 1029> frame{};
  for (std::size_t i = 0; i < frame.size(); ++i)
    frame[i] = static_cast<std::uint8_t>(i);
  Writer writer;
  espnow_rtcm_output_ops_t ops{Write, Clock, Idle, &writer};
  auto result = espnow_rtcm_output_frame(&ops, frame.data(), frame.size());
  assert(result.complete && result.accepted == frame.size());
  assert(writer.bytes == std::vector<std::uint8_t>(frame.begin(), frame.end()));
  writer = {};
  writer.now = UINT32_MAX - 10;
  result = espnow_rtcm_output_frame(&ops, frame.data(), frame.size());
  assert(result.complete && writer.bytes.size() == frame.size());
  for (unsigned mode = 1; mode <= 4; ++mode)
  {
    writer = {};
    writer.mode = mode;
    result = espnow_rtcm_output_frame(&ops, frame.data(), frame.size());
    assert(!result.complete && result.accepted == (mode == 2 ? 48 : 0));
    assert(writer.bytes.size() == result.accepted);  // No padding, retry duplication or log text.
  }
  writer = {};
  writer.mode = 1;
  writer.frozen = true;
  assert(!espnow_rtcm_output_frame(&ops, frame.data(), frame.size()).complete &&
         writer.calls == 4096);
  assert(!espnow_rtcm_output_frame(nullptr, frame.data(), frame.size()).complete);
  assert(!espnow_rtcm_output_frame(&ops, nullptr, frame.size()).complete);
  assert(!espnow_rtcm_output_frame(&ops, frame.data(), 0).complete);
  assert(!espnow_rtcm_output_frame(&ops, frame.data(), 1030).complete);
}
