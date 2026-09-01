#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "universal_gnss_ntrip/ntrip_config.hpp"

namespace universal_gnss_ntrip
{

enum class NtripCasterOrchestratorError : std::uint8_t
{
  kNone = 0,
  kEmptyCasterSet = 1,
  kDuplicateSource = 2,
};

struct NtripCasterOrchestratorMetrics
{
  std::uint64_t selections{0u};
  std::uint64_t failovers{0u};
};

// Selects an ordered NTRIP source without owning transport I/O, retries, or TLS.
class NtripCasterOrchestrator
{
public:
  NtripCasterOrchestrator() = default;
  explicit NtripCasterOrchestrator(std::vector<NtripConfig> casters);

  NtripCasterOrchestratorError set_casters(std::vector<NtripConfig> casters);
  NtripCasterOrchestratorError SelectInitial();
  NtripCasterOrchestratorError Failover();
  NtripCasterOrchestratorError ReselectActive();

  const NtripConfig* active_config() const;
  std::optional<NtripSourceIdentity> active_source_identity() const;
  std::optional<std::size_t> active_index() const;
  std::uint64_t active_incarnation() const;
  const NtripCasterOrchestratorMetrics& metrics() const;

private:
  NtripCasterOrchestratorError SelectIndex(std::size_t index, bool is_failover);
  void ClearActiveSource();

  std::vector<NtripConfig> casters_{};
  std::optional<std::size_t> active_index_{};
  std::uint64_t active_incarnation_{0u};
  NtripCasterOrchestratorMetrics metrics_{};
};

}  // namespace universal_gnss_ntrip
