#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/nmea_records.hpp"
#include "universal_gnss_protocols/parser_result.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"

namespace universal_gnss_protocols
{

bool IsNmeaSentenceType(const NmeaSentence& sentence, std::string_view sentence_type);

bool IsNmeaGst(const NmeaSentence& sentence);
bool IsNmeaVtg(const NmeaSentence& sentence);

std::optional<double> ParseNmeaDegreesMinutes(std::string_view field, std::size_t degree_digits);

std::optional<double> ParseNmeaLatitude(std::string_view field, std::string_view hemisphere);

std::optional<double> ParseNmeaLongitude(std::string_view field, std::string_view hemisphere);

ParserResult<NmeaGgaRecord> ParseNmeaGga(const NmeaSentence& sentence);

ParserResult<NmeaRmcRecord> ParseNmeaRmc(const NmeaSentence& sentence);

ParserResult<NmeaGsaRecord> ParseNmeaGsa(const NmeaSentence& sentence);

ParserResult<NmeaGsvRecord> ParseNmeaGsv(const NmeaSentence& sentence);

ParserResult<NmeaGstRecord> ParseNmeaGst(const NmeaSentence& sentence);

ParserResult<NmeaVtgRecord> ParseNmeaVtg(const NmeaSentence& sentence);

universal_gnss::GnssRuntimeState NmeaGgaToRuntimeState(const NmeaGgaRecord& record);

universal_gnss::GnssRuntimeState NmeaRmcToRuntimeState(const NmeaRmcRecord& record);

universal_gnss::GnssRuntimeState NmeaGsaToRuntimeState(const NmeaGsaRecord& record);

universal_gnss::GnssRuntimeState NmeaGstToRuntimeState(const NmeaGstRecord& record);

void MergeNmeaGsaIntoRuntimeState(const NmeaGsaRecord& record,
                                  universal_gnss::GnssRuntimeState& state);

void MergeNmeaGsvIntoRuntimeState(const NmeaGsvRecord& record,
                                  universal_gnss::GnssRuntimeState& state);

void MergeNmeaGstIntoRuntimeState(const NmeaGstRecord& record,
                                  universal_gnss::GnssRuntimeState& state);

}  // namespace universal_gnss_protocols
