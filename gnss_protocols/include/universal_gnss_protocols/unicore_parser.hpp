#pragma once

#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/unicore_binary_records.hpp"
#include "universal_gnss_protocols/unicore_records.hpp"

namespace universal_gnss_protocols
{

ParserResult<UnicorePvtslnRecord> ParseUnicorePvtsln(const UnicoreFrame& frame);

ParserResult<UnicoreBestNavRecord> ParseUnicoreBestNav(const UnicoreFrame& frame);

ParserResult<UnicoreRtkStatusRecord> ParseUnicoreRtkStatus(const UnicoreFrame& frame);

ParserResult<UnicoreRtcmStatusRecord> ParseUnicoreRtcmStatus(const UnicoreFrame& frame);

ParserResult<UnicoreBestSatRecord> ParseUnicoreBestSat(const UnicoreFrame& frame);

ParserResult<UnicoreSatsInfoRecord> ParseUnicoreSatsInfo(const UnicoreFrame& frame);

ParserResult<UnicoreJamStatusRecord> ParseUnicoreJamStatus(const UnicoreFrame& frame);

ParserResult<UnicoreFreqJamStatusRecord> ParseUnicoreFreqJamStatus(const UnicoreFrame& frame);

ParserResult<UnicoreHwStatusRecord> ParseUnicoreHwStatus(const UnicoreFrame& frame);

ParserResult<UnicoreAgcRecord> ParseUnicoreAgc(const UnicoreFrame& frame);

ParserResult<UnicoreBestNavBRecord> ParseUnicoreBestNavB(const UnicoreBinaryFrame& frame);

ParserResult<UnicorePvtslnBRecord> ParseUnicorePvtslnB(const UnicoreBinaryFrame& frame);

universal_gnss::GnssRuntimeState UnicorePvtslnToRuntimeState(const UnicorePvtslnRecord& record);

universal_gnss::GnssRuntimeState UnicoreBestNavToRuntimeState(const UnicoreBestNavRecord& record);

universal_gnss::GnssRuntimeState UnicoreBestNavBToRuntimeState(
    const UnicoreBestNavBRecord& record);

universal_gnss::GnssRuntimeState UnicorePvtslnBToRuntimeState(
    const UnicorePvtslnBRecord& record);

universal_gnss::GnssRuntimeState UnicoreRtkStatusToRuntimeState(
    const UnicoreRtkStatusRecord& record);

universal_gnss::GnssRuntimeState UnicoreRtcmStatusToRuntimeState(
    const UnicoreRtcmStatusRecord& record);

universal_gnss::GnssRuntimeState UnicoreBestSatToRuntimeState(
    const UnicoreBestSatRecord& record);

universal_gnss::GnssRuntimeState UnicoreSatsInfoToRuntimeState(
    const UnicoreSatsInfoRecord& record);

universal_gnss::GnssRuntimeState UnicoreJamStatusToRuntimeState(
    const UnicoreJamStatusRecord& record);

universal_gnss::GnssRuntimeState UnicoreFreqJamStatusToRuntimeState(
    const UnicoreFreqJamStatusRecord& record);

universal_gnss::GnssDiagnosticEvent UnicoreJamStatusToDiagnosticEvent(
    const UnicoreJamStatusRecord& record);

universal_gnss::GnssDiagnosticEvent UnicoreFreqJamStatusToDiagnosticEvent(
    const UnicoreFreqJamStatusRecord& record);

universal_gnss::GnssDiagnosticEvent UnicoreHwStatusToDiagnosticEvent(
    const UnicoreHwStatusRecord& record);

}  // namespace universal_gnss_protocols
