#pragma once

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"

namespace universal_gnss_protocols
{

ParserResult<UbxAckRecord> ParseUbxAck(const UbxFrame& frame);

ParserResult<UbxNavStatusRecord> ParseUbxNavStatus(const UbxFrame& frame);

ParserResult<UbxNavPvtRecord> ParseUbxNavPvt(const UbxFrame& frame);

ParserResult<UbxNavSatRecord> ParseUbxNavSat(const UbxFrame& frame);

ParserResult<UbxMonRfRecord> ParseUbxMonRf(const UbxFrame& frame);

universal_gnss::GnssRuntimeState UbxNavStatusToRuntimeState(const UbxNavStatusRecord& record);

universal_gnss::GnssRuntimeState UbxNavPvtToRuntimeState(const UbxNavPvtRecord& record);

universal_gnss::GnssRuntimeState UbxNavSatToRuntimeState(const UbxNavSatRecord& record);

universal_gnss::GnssRuntimeState UbxMonRfToRuntimeState(const UbxMonRfRecord& record);

}  // namespace universal_gnss_protocols
