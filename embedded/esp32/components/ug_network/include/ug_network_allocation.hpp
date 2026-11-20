// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace ug::embedded
{
// Task-only S3 network children (strings, vectors, protocol-monitor payload).
// Never use this domain around ISR, RTOS/DMA control creation, or SDK drivers.
inline thread_local bool network_external_allocation = false;
inline thread_local unsigned network_allocation_owner = 0;
struct NetworkAllocationStats
{
  std::size_t live{}, requested_bytes{}, peak_requested_bytes{};
  std::uint32_t allocations{}, refusals{};
};
std::array<NetworkAllocationStats, 8> NetworkCppAllocations();
struct NetworkArenaStats
{
  std::size_t reserved{}, free{}, minimum{}, largest{};
};
// On-demand diagnostic walk, not a real-time allocation-path operation.
NetworkArenaStats NetworkArenaMemory();
#if UG_NETWORK_ALLOCATION_TEST
// Test-only: caller must quiesce allocation traffic and all other heap checkers.
bool CheckNetworkArenaIntegrity();
#endif
class NetworkAllocationScope
{
public:
  explicit NetworkAllocationScope(unsigned owner = 0)
      : previous_(network_external_allocation), previous_owner_(network_allocation_owner)
  {
    network_external_allocation = true;
    network_allocation_owner = owner < 8 ? owner : 0;
  }
  ~NetworkAllocationScope()
  {
    network_external_allocation = previous_;
    network_allocation_owner = previous_owner_;
  }
  NetworkAllocationScope(const NetworkAllocationScope&) = delete;
  NetworkAllocationScope& operator=(const NetworkAllocationScope&) = delete;

private:
  bool previous_;
  unsigned previous_owner_;
};
// Link anchor: the replacement operators must actually be linked, not discarded
// from a static archive. No allocator policy changes until a scope is entered.
bool InitializeNetworkAllocation();
#if UG_NETWORK_ALLOCATION_TEST
// Deterministic cold-unwind regression; never enabled in shipping firmware.
void FailNextNetworkAllocationForTest(unsigned owner);
#endif
}  // namespace ug::embedded
