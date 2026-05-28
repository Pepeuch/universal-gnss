#pragma once

#include <vector>

#include "universal_gnss_protocols/parser_base.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"

namespace universal_gnss_protocols
{

class UnicoreFrameFramer : public StreamParserBase<UnicoreFrame>
{
public:
  explicit UnicoreFrameFramer(std::size_t max_frame_length = 2048);

  ParserResult<UnicoreFrame> PushByte(
      std::uint8_t byte,
      std::optional<ProtocolTimestampNs> timestamp_ns = std::nullopt) override;

  ParserResult<UnicoreFrame> Finalize() override;

  void Reset() override;

private:
  UnicoreFrame BuildFrame() const;

  static bool IsSyncByte(std::uint8_t byte);

  std::size_t max_frame_length_;
  std::vector<std::uint8_t> buffer_;
  std::optional<ProtocolTimestampNs> frame_timestamp_ns_{};
};

}  // namespace universal_gnss_protocols
