// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss_transport/byte_stream.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace ug::embedded
{
struct UartDrainResult
{
  std::size_t bytes{}, batches{};
  bool failed{}, budget_exhausted{};
};
// First read may wait; subsequent reads must be nonblocking. Each callback must
// copy/consume its borrowed batch before returning. One owner, no allocation.
template <typename Read, typename Consume>
UartDrainResult DrainUart(Read&& read, Consume&& consume)
{
  UartDrainResult result;
  std::array<std::uint8_t, 512> bytes{};
  constexpr std::size_t kMaxBatches = 8;
  while (result.batches < kMaxBatches)
  {
    const auto received = read(bytes.data(), bytes.size(), result.batches == 0);
    if (received.status != universal_gnss_transport::TransportStatus::kOk ||
        received.bytes_read > bytes.size())
    {
      result.failed = true;
      break;
    }
    if (!received.bytes_read)
      break;
    consume(bytes.data(), received.bytes_read);
    result.bytes += received.bytes_read;
    ++result.batches;
  }
  result.budget_exhausted = result.batches == kMaxBatches;
  return result;
}
}  // namespace ug::embedded
