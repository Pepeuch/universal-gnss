#pragma once

#include <optional>
#include <utility>

#include "universal_gnss_protocols/parser_status.hpp"

namespace universal_gnss_protocols
{

template <typename RecordT>
struct ParserResult
{
  ParserStatus status{ParserStatus::kIdle};
  std::optional<RecordT> record{};

  static ParserResult NeedMoreData()
  {
    return ParserResult{ParserStatus::kNeedMoreData, std::nullopt};
  }

  static ParserResult RecordReady(RecordT record_value)
  {
    return ParserResult{ParserStatus::kRecordReady, std::move(record_value)};
  }

  static ParserResult Skipped()
  {
    return ParserResult{ParserStatus::kSkipped, std::nullopt};
  }

  static ParserResult InvalidData()
  {
    return ParserResult{ParserStatus::kInvalidData, std::nullopt};
  }

  static ParserResult Truncated()
  {
    return ParserResult{ParserStatus::kTruncated, std::nullopt};
  }

  static ParserResult Overflow()
  {
    return ParserResult{ParserStatus::kOverflow, std::nullopt};
  }
};

}  // namespace universal_gnss_protocols
