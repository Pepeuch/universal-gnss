#pragma once

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/unicore_records.hpp"

namespace universal_gnss_protocols
{

ParserResult<UnicorePvtslnRecord> ParseUnicorePvtsln(const UnicoreFrame& frame);

ParserResult<UnicoreBestNavRecord> ParseUnicoreBestNav(const UnicoreFrame& frame);

ParserResult<UnicoreRtkStatusRecord> ParseUnicoreRtkStatus(const UnicoreFrame& frame);

ParserResult<UnicoreRtcmStatusRecord> ParseUnicoreRtcmStatus(const UnicoreFrame& frame);

universal_gnss::GnssRuntimeState UnicorePvtslnToRuntimeState(const UnicorePvtslnRecord& record);

universal_gnss::GnssRuntimeState UnicoreBestNavToRuntimeState(const UnicoreBestNavRecord& record);

universal_gnss::GnssRuntimeState UnicoreRtkStatusToRuntimeState(
    const UnicoreRtkStatusRecord& record);

universal_gnss::GnssRuntimeState UnicoreRtcmStatusToRuntimeState(
    const UnicoreRtcmStatusRecord& record);

}  // namespace universal_gnss_protocols
