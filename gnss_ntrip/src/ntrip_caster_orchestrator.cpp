#include "universal_gnss_ntrip/ntrip_caster_orchestrator.hpp"

#include <utility>

namespace universal_gnss_ntrip
{

NtripCasterOrchestrator::NtripCasterOrchestrator(std::vector<NtripConfig> casters)
{
  (void)set_casters(std::move(casters));
}

NtripCasterOrchestratorError NtripCasterOrchestrator::set_casters(
    std::vector<NtripConfig> casters)
{
  ClearActiveSource();
  casters_.clear();
  if (casters.empty())
  {
    return NtripCasterOrchestratorError::kEmptyCasterSet;
  }

  for (std::size_t index = 0u; index < casters.size(); ++index)
  {
    const NtripSourceIdentity identity = BuildNtripSourceIdentity(casters[index]);
    for (std::size_t prior = 0u; prior < index; ++prior)
    {
      if (BuildNtripSourceIdentity(casters[prior]) == identity)
      {
        return NtripCasterOrchestratorError::kDuplicateSource;
      }
    }
  }
  casters_ = std::move(casters);
  return NtripCasterOrchestratorError::kNone;
}

NtripCasterOrchestratorError NtripCasterOrchestrator::SelectInitial()
{
  return SelectIndex(0u, false);
}

NtripCasterOrchestratorError NtripCasterOrchestrator::Failover()
{
  if (!active_index_.has_value())
  {
    return SelectInitial();
  }
  return SelectIndex((*active_index_ + 1u) % casters_.size(), true);
}

NtripCasterOrchestratorError NtripCasterOrchestrator::ReselectActive()
{
  return active_index_.has_value() ? SelectIndex(*active_index_, false) : SelectInitial();
}

const NtripConfig* NtripCasterOrchestrator::active_config() const
{
  return active_index_.has_value() ? &casters_[*active_index_] : nullptr;
}

std::optional<NtripSourceIdentity> NtripCasterOrchestrator::active_source_identity() const
{
  const NtripConfig* config = active_config();
  return config == nullptr ? std::nullopt
                           : std::optional<NtripSourceIdentity>{BuildNtripSourceIdentity(*config)};
}

std::optional<std::size_t> NtripCasterOrchestrator::active_index() const
{
  return active_index_;
}

std::uint64_t NtripCasterOrchestrator::active_incarnation() const
{
  return active_incarnation_;
}

const NtripCasterOrchestratorMetrics& NtripCasterOrchestrator::metrics() const
{
  return metrics_;
}

NtripCasterOrchestratorError NtripCasterOrchestrator::SelectIndex(const std::size_t index,
                                                                    const bool is_failover)
{
  if (casters_.empty())
  {
    ClearActiveSource();
    return NtripCasterOrchestratorError::kEmptyCasterSet;
  }
  active_index_ = index;
  ++active_incarnation_;
  ++metrics_.selections;
  if (is_failover)
  {
    ++metrics_.failovers;
  }
  return NtripCasterOrchestratorError::kNone;
}

void NtripCasterOrchestrator::ClearActiveSource()
{
  active_index_.reset();
}

}  // namespace universal_gnss_ntrip
