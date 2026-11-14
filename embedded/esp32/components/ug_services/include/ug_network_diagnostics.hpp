// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
namespace ug::embedded
{
struct NetworkOwnerStats
{
  std::uint64_t configuration_revision{}, link_generation{};
  std::int64_t receipt_ns{};
  std::uint64_t frames{}, bytes{}, connections{}, failures{}, expired{};
  bool sampled{}, current{}, active{};
};
inline bool NetworkSampleCurrent(const NetworkOwnerStats& sample,
                                 std::uint64_t revision,
                                 std::uint64_t link,
                                 bool available,
                                 std::int64_t now_ns)
{
  return sample.sampled && available && sample.configuration_revision == revision &&
         sample.link_generation == link && sample.receipt_ns >= 0 && now_ns >= sample.receipt_ns &&
         now_ns - sample.receipt_ns <= 5000000000LL;
}
struct NetworkServiceStats
{
  bool network_ready{}, upstream_flowing{};
  std::uint64_t link_generation{}, configuration_revision{};
  NetworkOwnerStats upstream, caster;
  std::array<NetworkOwnerStats, 5> publishers{};
  std::uint32_t upstream_error{};
  std::uint64_t upstream_gga_sent{}, upstream_gga_errors{};
};
inline NetworkServiceStats ProjectNetworkStats(NetworkServiceStats stats,
                                               std::uint64_t revision,
                                               std::uint64_t link,
                                               bool available,
                                               std::int64_t now_ns)
{
  stats.network_ready = available;
  stats.link_generation = link;
  stats.configuration_revision = revision;
  const auto project = [=](NetworkOwnerStats& sample) {
    sample.current = NetworkSampleCurrent(sample, revision, link, available, now_ns);
    sample.active = sample.active && sample.current;
  };
  project(stats.upstream);
  project(stats.caster);
  for (auto& publisher : stats.publishers)
    project(publisher);
  stats.upstream_flowing =
      stats.upstream_flowing && stats.upstream.current && stats.upstream.active;
  return stats;
}
}  // namespace ug::embedded
