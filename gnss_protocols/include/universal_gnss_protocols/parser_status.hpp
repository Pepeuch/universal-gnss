#pragma once

namespace universal_gnss_protocols
{

enum class ParserStatus
{
  kIdle = 0,
  kNeedMoreData,
  kRecordReady,
  kSkipped,
  kInvalidData,
  kTruncated,
  kOverflow,
};

}  // namespace universal_gnss_protocols
