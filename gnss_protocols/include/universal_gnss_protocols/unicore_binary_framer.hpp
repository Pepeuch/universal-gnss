#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "universal_gnss_protocols/parser_base.hpp"
#include "universal_gnss_protocols/unicore_binary_records.hpp"

namespace universal_gnss_protocols
{

std::uint32_t ComputeUnicoreBinaryCrc32(const std::uint8_t* data, std::size_t size);

bool ValidateUnicoreBinaryCrc32(
    const std::uint8_t* data,
    std::size_t size,
    std::uint32_t expected_crc32);

class UnicoreBinaryFrameFramer : public StreamParserBase<UnicoreBinaryFrame>
{
public:
  explicit UnicoreBinaryFrameFramer(std::size_t max_frame_length = 65536u);

  ParserResult<UnicoreBinaryFrame> PushByte(
      std::uint8_t byte,
      std::optional<ProtocolTimestampNs> timestamp_ns = std::nullopt) override;

  ParserResult<UnicoreBinaryFrame> Finalize() override;

  void Reset() override;

private:
  ParserResult<UnicoreBinaryFrame> StartSync(
      std::uint8_t byte,
      std::optional<ProtocolTimestampNs> timestamp_ns);

  void AppendByte(std::uint8_t byte, std::optional<ProtocolTimestampNs> timestamp_ns);

  std::optional<UnicoreBinaryFrame> FindEmbeddedValidFrame() const;

  UnicoreBinaryFrame BuildFrame() const;
  UnicoreBinaryFrame BuildFrame(
      std::size_t frame_offset,
      std::size_t frame_size,
      std::optional<ProtocolTimestampNs> timestamp_ns) const;

  std::size_t max_frame_length_;
  ByteVector buffer_{};
  std::vector<std::optional<ProtocolTimestampNs>> byte_timestamps_{};
  std::optional<ProtocolTimestampNs> frame_timestamp_ns_{};
  std::size_t expected_frame_size_{0u};
};

}  // namespace universal_gnss_protocols
