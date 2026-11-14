// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_network_budget.hpp"
#include <cassert>
#include <initializer_list>
using namespace ug::embedded;
int main()
{
  constexpr auto gateway = RequiredNetworkBudget(5, true, 3, false, 4);
  constexpr auto relay = RequiredNetworkBudget(1, false, 3, false, 4);
  static_assert(gateway.valid && gateway.sockets == 20 && gateway.tcp_active == 16 &&
                gateway.tcp_listeners == 2 && gateway.udp_pcbs == 13);
  static_assert(relay.valid && relay.sockets == 10 && relay.tcp_active == 7 &&
                relay.tcp_listeners == 1 && relay.udp_pcbs == 13);
  static_assert(!NetworkBudgetFits({gateway, 10, 16, 16, 16}));  // Actual previous SDK default.
  for (const auto required : {gateway, relay})
  {
    NetworkCapacity exact{
        required, required.sockets, required.tcp_active, required.tcp_listeners, required.udp_pcbs};
    assert(NetworkBudgetFits(exact));
    --exact.sockets;
    assert(!NetworkBudgetFits(exact));
    ++exact.sockets;
    --exact.tcp_active;
    assert(!NetworkBudgetFits(exact));
    ++exact.tcp_active;
    --exact.tcp_listeners;
    assert(!NetworkBudgetFits(exact));
    ++exact.tcp_listeners;
    --exact.udp_pcbs;
    assert(!NetworkBudgetFits(exact));
  }
  const auto socket_mdns = RequiredNetworkBudget(5, true, 3, true, 4);
  assert(socket_mdns.sockets == 23 &&
         socket_mdns.udp_pcbs == 15);  // One dual-stack FD per interface.
  const auto more_dns = RequiredNetworkBudget(5, true, 3, false, 8);
  assert(more_dns.sockets == 20 && more_dns.udp_pcbs == 17);
  assert(!RequiredNetworkBudget(6, true, 3, false, 4).valid);
  assert(!RequiredNetworkBudget(5, true, 4, false, 4).valid);
  assert(!RequiredNetworkBudget(5, true, 3, false, 256).valid);
  assert(RequiredNetworkBudget(1, false, 0, false, 0).udp_pcbs == 8);
  assert(!NetworkBudgetFits({{}, 100, 100, 100, 100}));
}
