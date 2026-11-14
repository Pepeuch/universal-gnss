// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss/gnss_runtime_state.hpp"
namespace ug::embedded
{
// IDF monotonic receipt metadata comes from the UG position-observation tracker,
// never from a periodic publication, receiver UTC, or a changed numeric position.
inline universal_gnss::GnssRuntimeState
FreshPositionForGga(universal_gnss::GnssRuntimeState state,
                    std::optional<std::int64_t> last_position_ns,
                    std::int64_t now_ns)
{
  if (!last_position_ns || now_ns < *last_position_ns || now_ns - *last_position_ns > 3000000000LL)
  {
    state.fix_valid = false;
    universal_gnss::ClearPositionValues(state);
  }
  return state;
}
}  // namespace ug::embedded
