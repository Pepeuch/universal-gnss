#pragma once

#include <cstdint>
#include <optional>

#include "universal_gnss_protocols/parser_result.hpp"

namespace universal_gnss_protocols
{

using ProtocolTimestampNs = std::int64_t;

template <typename RecordT>
class StreamParserBase
{
public:
  virtual ~StreamParserBase() = default;

  virtual ParserResult<RecordT> PushByte(
      std::uint8_t byte,
      std::optional<ProtocolTimestampNs> timestamp_ns = std::nullopt) = 0;

  virtual ParserResult<RecordT> Finalize() = 0;

  virtual void Reset() = 0;
};

}  // namespace universal_gnss_protocols
