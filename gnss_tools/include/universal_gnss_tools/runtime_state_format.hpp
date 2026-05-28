#pragma once

#include <optional>
#include <string>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_driver/receiver_session.hpp"

namespace universal_gnss_tools
{

std::string FormatRuntimeStateCompact(
    const universal_gnss::GnssRuntimeState& state,
    std::optional<universal_gnss_driver::ReceiverSessionKind> selected_session_kind = std::nullopt);

std::string FormatRuntimeStateJson(
    const universal_gnss::GnssRuntimeState& state,
    std::optional<universal_gnss_driver::ReceiverSessionKind> selected_session_kind = std::nullopt);

}  // namespace universal_gnss_tools
