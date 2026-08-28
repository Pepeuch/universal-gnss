#include "universal_gnss_protocols/nmea_framer.hpp"

#include <string_view>

#include "universal_gnss_protocols/nmea_checksum.hpp"

namespace universal_gnss_protocols
{

namespace
{

std::size_t TrimLineEnding(const std::vector<std::uint8_t>& buffer)
{
  std::size_t end = buffer.size();
  while (end > 0 && (buffer[end - 1] == '\r' || buffer[end - 1] == '\n'))
  {
    --end;
  }
  return end;
}

}  // namespace

NmeaSentenceFramer::NmeaSentenceFramer(std::size_t max_frame_length)
    : max_frame_length_(max_frame_length)
{
}

ParserResult<NmeaSentence> NmeaSentenceFramer::PushByte(
    std::uint8_t byte,
    std::optional<ProtocolTimestampNs> timestamp_ns)
{
  if (buffer_.empty())
  {
    if (byte != '$' && byte != '!')
    {
      return ParserResult<NmeaSentence>::Skipped();
    }

    buffer_.push_back(byte);
    frame_timestamp_ns_ = timestamp_ns;
    return ParserResult<NmeaSentence>::NeedMoreData();
  }

  if (byte == '$' || byte == '!')
  {
    buffer_.assign(1u, byte);
    frame_timestamp_ns_ = timestamp_ns;
    return ParserResult<NmeaSentence>::NeedMoreData();
  }

  buffer_.push_back(byte);
  if (buffer_.size() > max_frame_length_)
  {
    Reset();
    return ParserResult<NmeaSentence>::Overflow();
  }

  if (byte != '\n')
  {
    return ParserResult<NmeaSentence>::NeedMoreData();
  }

  NmeaSentence sentence = BuildSentence();
  Reset();
  return ParserResult<NmeaSentence>::RecordReady(std::move(sentence));
}

ParserResult<NmeaSentence> NmeaSentenceFramer::Finalize()
{
  if (buffer_.empty())
  {
    return ParserResult<NmeaSentence>{};
  }

  Reset();
  return ParserResult<NmeaSentence>::Truncated();
}

void NmeaSentenceFramer::Reset()
{
  buffer_.clear();
  frame_timestamp_ns_.reset();
}

NmeaSentence NmeaSentenceFramer::BuildSentence() const
{
  NmeaSentence sentence;
  sentence.timestamp_ns = frame_timestamp_ns_;
  sentence.leader = static_cast<char>(buffer_.front());
  sentence.raw_bytes = buffer_;

  const std::size_t end = TrimLineEnding(buffer_);
  const std::string_view frame(reinterpret_cast<const char*>(buffer_.data()), end);
  const std::size_t star = frame.find('*');
  const std::size_t payload_end = (star == std::string_view::npos) ? frame.size() : star;
  if (payload_end > 1)
  {
    sentence.payload_text.assign(frame.substr(1, payload_end - 1));
  }

  if (!sentence.payload_text.empty())
  {
    const std::string_view payload_text(sentence.payload_text);
    const std::size_t comma = payload_text.find(',');
    const std::string_view header =
        payload_text.substr(0, comma == std::string_view::npos ? payload_text.size() : comma);
    if (header.size() >= 5)
    {
      sentence.talker.assign(header.substr(0, 2));
      sentence.sentence_type.assign(header.substr(2));
    }
    else
    {
      sentence.sentence_type.assign(header);
    }
  }

  sentence.checksum_status = ValidateNmeaChecksum(
      frame, &sentence.reported_checksum, &sentence.computed_checksum);
  return sentence;
}

}  // namespace universal_gnss_protocols
