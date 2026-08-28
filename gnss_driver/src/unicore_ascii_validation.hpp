#pragma once

#include "universal_gnss_protocols/parser_status.hpp"
#include "universal_gnss_protocols/unicore_parser.hpp"

namespace universal_gnss_driver
{
namespace detail
{

template <typename RecordT>
bool IsParsedUnicoreAsciiRecord(
    const universal_gnss_protocols::ParserResult<RecordT>& result)
{
  return result.status == universal_gnss_protocols::ParserStatus::kRecordReady &&
         result.record.has_value();
}

inline bool IsVerifiedUnicoreAsciiRecord(
    const universal_gnss_protocols::UnicoreFrame& frame)
{
  using universal_gnss_protocols::ChecksumStatus;

  if (frame.sync_char != '#' || frame.checksum_status != ChecksumStatus::kValid)
  {
    return false;
  }

  if (frame.message_name == "PVTSLNA")
  {
    return IsParsedUnicoreAsciiRecord(universal_gnss_protocols::ParseUnicorePvtsln(frame));
  }
  if (frame.message_name == "BESTNAVA")
  {
    return IsParsedUnicoreAsciiRecord(universal_gnss_protocols::ParseUnicoreBestNav(frame));
  }
  if (frame.message_name == "RTKSTATUSA")
  {
    return IsParsedUnicoreAsciiRecord(universal_gnss_protocols::ParseUnicoreRtkStatus(frame));
  }
  if (frame.message_name == "RTCMSTATUSA")
  {
    return IsParsedUnicoreAsciiRecord(universal_gnss_protocols::ParseUnicoreRtcmStatus(frame));
  }
  if (frame.message_name == "BESTSATA")
  {
    return IsParsedUnicoreAsciiRecord(universal_gnss_protocols::ParseUnicoreBestSat(frame));
  }
  if (frame.message_name == "SATSINFOA")
  {
    return IsParsedUnicoreAsciiRecord(universal_gnss_protocols::ParseUnicoreSatsInfo(frame));
  }
  if (frame.message_name == "JAMSTATUSA")
  {
    return IsParsedUnicoreAsciiRecord(universal_gnss_protocols::ParseUnicoreJamStatus(frame));
  }
  if (frame.message_name == "FREQJAMSTATUSA")
  {
    return IsParsedUnicoreAsciiRecord(
        universal_gnss_protocols::ParseUnicoreFreqJamStatus(frame));
  }
  if (frame.message_name == "HWSTATUSA")
  {
    return IsParsedUnicoreAsciiRecord(universal_gnss_protocols::ParseUnicoreHwStatus(frame));
  }
  if (frame.message_name == "AGCA")
  {
    return IsParsedUnicoreAsciiRecord(universal_gnss_protocols::ParseUnicoreAgc(frame));
  }

  return false;
}

}  // namespace detail
}  // namespace universal_gnss_driver
