#pragma once

#include <vector>

#include "universal_gnss_protocols/parser_base.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"

namespace universal_gnss_protocols
{

class UbxFrameFramer : public StreamParserBase<UbxFrame>
{
public:
  explicit UbxFrameFramer(std::size_t max_frame_length = 4096);

  ParserResult<UbxFrame> PushByte(
      std::uint8_t byte,
      std::optional<ProtocolTimestampNs> timestamp_ns = std::nullopt) override;

  ParserResult<UbxFrame> Finalize() override;

  void Reset() override;

private:
  ParserResult<UbxFrame> StartSync(
      std::uint8_t byte,
      std::optional<ProtocolTimestampNs> timestamp_ns);

  UbxFrame BuildFrame() const;

  std::size_t max_frame_length_;
  std::vector<std::uint8_t> buffer_;
  std::optional<ProtocolTimestampNs> frame_timestamp_ns_{};
  std::size_t expected_frame_size_{0};
};

}  // namespace universal_gnss_protocols
