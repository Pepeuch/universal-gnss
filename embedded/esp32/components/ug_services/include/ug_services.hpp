// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
#include "universal_gnss/gnss_runtime_state.hpp"
namespace ug::embedded
{
esp_err_t StartNetworkServices();
// IDF event task may invalidate sources while a network worker is inside connect.
void NetworkAvailabilityChanged(bool available);
void RestartNetworkServices();
void PublishGnssSnapshot(const universal_gnss::GnssRuntimeState& state,
                         std::optional<std::int64_t> position_receipt_ns = std::nullopt);
universal_gnss::GnssRuntimeState GnssSnapshot();
struct GnssObservationSnapshot
{
  universal_gnss::GnssRuntimeState state;
  std::optional<std::int64_t> position_receipt_ns;
};
GnssObservationSnapshot GnssObservation();
struct NetworkServiceStats
{
  bool upstream_connected{}, upstream_flowing{}, caster_running{};
  std::uint64_t upstream_frames{}, publisher_frames[5]{}, publisher_failures[5]{}, caster_frames{};
};
NetworkServiceStats NetworkStats();
}  // namespace ug::embedded
