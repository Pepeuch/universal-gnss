// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
namespace ug::embedded
{
// Owning service limits: changing a client limit must also change the budget.
constexpr std::size_t kHttpsClientLimit = 3, kHttpsInternalSockets = 3;
constexpr std::size_t kCasterClientLimit = 4;
constexpr std::size_t kNetworkSocketReserve = 2;
constexpr std::size_t kDhcpUdpBudget = 3;  // Shared DHCPv4/DHCPv6 clients and the one AP server.
struct NetworkBudget
{
  std::size_t sockets{}, tcp_active{}, tcp_listeners{}, udp_pcbs{};
  bool valid{};
};
constexpr NetworkBudget RequiredNetworkBudget(std::size_t publishers,
                                              bool caster,
                                              std::size_t mdns_interfaces,
                                              bool mdns_socket_mode,
                                              std::size_t dns_source_ports)
{
  if (publishers > 5 || mdns_interfaces > 3 || dns_source_ports > 255)
    return {};
  const auto ntrip = 1 + publishers;
  // Caster accepts one additional FD before rejecting a client over its limit.
  const auto caster_tcp = caster ? kCasterClientLimit + 1 : 0;
  const auto mdns_fds = mdns_socket_mode ? mdns_interfaces : 0;
  // Pinned mdns 1.15.1 raw backend shares one PCB across interfaces/families.
  const auto mdns_udp = mdns_socket_mode ? mdns_interfaces : (mdns_interfaces ? 1u : 0u);
  return {ntrip + kHttpsInternalSockets + kHttpsClientLimit + (caster ? 1 + caster_tcp : 0) +
              mdns_fds + kNetworkSocketReserve,
          ntrip + kHttpsClientLimit + caster_tcp + kNetworkSocketReserve,
          1 + (caster ? 1u : 0u),
          2 + mdns_udp + dns_source_ports + 1 + kDhcpUdpBudget +
              kNetworkSocketReserve,  // HTTP controls, mDNS, DNS, SNTP, reserve.
          true};
}
struct NetworkCapacity
{
  NetworkBudget required;
  std::size_t sockets{}, tcp_active{}, tcp_listeners{}, udp_pcbs{};
};
constexpr bool NetworkBudgetFits(const NetworkCapacity& capacity)
{
  return capacity.required.valid && capacity.sockets >= capacity.required.sockets &&
         capacity.tcp_active >= capacity.required.tcp_active &&
         capacity.tcp_listeners >= capacity.required.tcp_listeners &&
         capacity.udp_pcbs >= capacity.required.udp_pcbs;
}
NetworkCapacity NetworkResourceCapacity();  // Static provisioned limits, never live availability.
}  // namespace ug::embedded
