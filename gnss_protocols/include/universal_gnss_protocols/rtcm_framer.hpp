#pragma once

#include <optional>
#include <vector>

#include "universal_gnss_protocols/parser_base.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"

namespace universal_gnss_protocols
{

class RtcmFrameFramer : public StreamParserBase<RtcmFrame>
{
public:
  explicit RtcmFrameFramer(std::size_t max_frame_length = 1029);

  ParserResult<RtcmFrame> PushByte(
      std::uint8_t byte,
      std::optional<ProtocolTimestampNs> timestamp_ns = std::nullopt) override;

  ParserResult<RtcmFrame> Finalize() override;

  void Reset() override;

private:
  ParserResult<RtcmFrame> StartFrame(
      std::uint8_t byte,
      std::optional<ProtocolTimestampNs> timestamp_ns);

  void AppendByte(std::uint8_t byte, std::optional<ProtocolTimestampNs> timestamp_ns);

  std::optional<RtcmFrame> FindEmbeddedValidFrame() const;

  RtcmFrame BuildFrame() const;
  RtcmFrame BuildFrame(
      std::size_t frame_offset,
      std::size_t frame_size,
      std::optional<ProtocolTimestampNs> timestamp_ns) const;

  std::size_t max_frame_length_;
  std::vector<std::uint8_t> buffer_;
  std::vector<std::optional<ProtocolTimestampNs>> byte_timestamps_;
  std::optional<ProtocolTimestampNs> frame_timestamp_ns_{};
  std::size_t expected_frame_size_{0};
};

}  // namespace universal_gnss_protocols
