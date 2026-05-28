#pragma once

#include <vector>

#include "universal_gnss_protocols/parser_base.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"

namespace universal_gnss_protocols
{

class NmeaSentenceFramer : public StreamParserBase<NmeaSentence>
{
public:
  explicit NmeaSentenceFramer(std::size_t max_frame_length = 512);

  ParserResult<NmeaSentence> PushByte(
      std::uint8_t byte,
      std::optional<ProtocolTimestampNs> timestamp_ns = std::nullopt) override;

  ParserResult<NmeaSentence> Finalize() override;

  void Reset() override;

private:
  NmeaSentence BuildSentence() const;

  std::size_t max_frame_length_;
  std::vector<std::uint8_t> buffer_;
  std::optional<ProtocolTimestampNs> frame_timestamp_ns_{};
};

}  // namespace universal_gnss_protocols
