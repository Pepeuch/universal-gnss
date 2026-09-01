#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_ntrip/ntrip_caster_orchestrator.hpp"

namespace
{
using universal_gnss_ntrip::NtripCasterOrchestrator;
using universal_gnss_ntrip::NtripCasterOrchestratorError;
using universal_gnss_ntrip::NtripConfig;

struct TestContext
{
  int failures{0};
  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

NtripConfig Caster(const std::string& host, const std::uint16_t port, const std::string& mountpoint)
{
  NtripConfig config;
  config.host = host;
  config.port = port;
  config.mountpoint = mountpoint;
  return config;
}

void TestSelectionFailoverAndIncarnation(TestContext& ctx)
{
  NtripCasterOrchestrator orchestrator(
      {Caster("FIRST.example", 2101u, "one"), Caster("second.example", 2201u, "/two")});
  ctx.Expect(orchestrator.SelectInitial() == NtripCasterOrchestratorError::kNone &&
                 orchestrator.active_index() == 0u && orchestrator.active_incarnation() == 1u &&
                 orchestrator.active_source_identity()->host == "first.example",
             "initial selection must choose the first normalized source with incarnation one");
  ctx.Expect(orchestrator.Failover() == NtripCasterOrchestratorError::kNone &&
                 orchestrator.active_index() == 1u && orchestrator.active_incarnation() == 2u &&
                 orchestrator.active_source_identity()->mountpoint == "/two",
             "failover must advance once to the next source and replace its identity");
  ctx.Expect(orchestrator.Failover() == NtripCasterOrchestratorError::kNone &&
                 orchestrator.active_index() == 0u && orchestrator.active_incarnation() == 3u &&
                 orchestrator.metrics().selections == 3u && orchestrator.metrics().failovers == 2u,
             "failover must wrap in stable order and update metrics");
  ctx.Expect(orchestrator.ReselectActive() == NtripCasterOrchestratorError::kNone &&
                 orchestrator.active_index() == 0u && orchestrator.active_incarnation() == 4u &&
                 orchestrator.metrics().failovers == 2u,
             "reselecting after reconnect must create a distinct active incarnation");
}

void TestInvalidSetsHaveNoActiveSource(TestContext& ctx)
{
  NtripCasterOrchestrator orchestrator;
  ctx.Expect(orchestrator.set_casters({}) == NtripCasterOrchestratorError::kEmptyCasterSet &&
                 !orchestrator.active_config() && !orchestrator.active_source_identity().has_value(),
             "empty caster sets must leave no active source");
  ctx.Expect(orchestrator.set_casters({Caster("same.example", 2101u, "mount"),
                                       Caster("SAME.example", 2101u, "/mount")}) ==
                     NtripCasterOrchestratorError::kDuplicateSource &&
                 !orchestrator.active_config(),
             "duplicate normalized source identities must be rejected without an active source");
}
}  // namespace

int main()
{
  TestContext ctx;
  TestSelectionFailoverAndIncarnation(ctx);
  TestInvalidSetsHaveNoActiveSource(ctx);
  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All NTRIP caster orchestrator tests passed\n";
  return EXIT_SUCCESS;
}
