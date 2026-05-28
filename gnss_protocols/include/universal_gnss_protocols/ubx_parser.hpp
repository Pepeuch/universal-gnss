#pragma once

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"

namespace universal_gnss_protocols
{

ParserResult<UbxNavPvtRecord> ParseUbxNavPvt(const UbxFrame& frame);

ParserResult<UbxNavSatRecord> ParseUbxNavSat(const UbxFrame& frame);

universal_gnss::GnssRuntimeState UbxNavPvtToRuntimeState(const UbxNavPvtRecord& record);

universal_gnss::GnssRuntimeState UbxNavSatToRuntimeState(const UbxNavSatRecord& record);

}  // namespace universal_gnss_protocols
