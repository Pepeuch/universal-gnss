#include "universal_gnss_protocols/nmea_checksum.hpp"

#include <algorithm>
#include <cctype>

namespace universal_gnss_protocols
{

namespace
{

std::uint8_t HexNibble(char c)
{
  if (c >= '0' && c <= '9')
  {
    return static_cast<std::uint8_t>(c - '0');
  }
  c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return static_cast<std::uint8_t>(10 + c - 'A');
}

std::size_t TrimLineEnding(std::string_view frame)
{
  std::size_t end = frame.size();
  while (end > 0 && (frame[end - 1] == '\r' || frame[end - 1] == '\n'))
  {
    --end;
  }
  return end;
}

}  // namespace

std::uint8_t ComputeNmeaChecksum(std::string_view payload_text)
{
  std::uint8_t checksum = 0;
  for (const char c : payload_text)
  {
    checksum ^= static_cast<std::uint8_t>(c);
  }
  return checksum;
}

bool TryParseHexByte(std::string_view text, std::uint8_t& value)
{
  if (text.size() != 2)
  {
    return false;
  }

  if (!std::isxdigit(static_cast<unsigned char>(text[0])) ||
      !std::isxdigit(static_cast<unsigned char>(text[1])))
  {
    return false;
  }

  value = static_cast<std::uint8_t>((HexNibble(text[0]) << 4) | HexNibble(text[1]));
  return true;
}

ChecksumStatus ValidateNmeaChecksum(std::string_view frame,
                                    std::optional<std::uint8_t>* reported_checksum,
                                    std::optional<std::uint8_t>* computed_checksum)
{
  if (reported_checksum != nullptr)
  {
    reported_checksum->reset();
  }
  if (computed_checksum != nullptr)
  {
    computed_checksum->reset();
  }

  if (frame.empty() || (frame.front() != '$' && frame.front() != '!'))
  {
    return ChecksumStatus::kInvalid;
  }

  const std::size_t end = TrimLineEnding(frame);
  const std::size_t star = frame.find('*');
  if (star == std::string_view::npos || star + 3 > end)
  {
    return ChecksumStatus::kMissing;
  }

  std::uint8_t reported = 0;
  if (!TryParseHexByte(frame.substr(star + 1, 2), reported))
  {
    return ChecksumStatus::kInvalid;
  }

  const std::uint8_t computed = ComputeNmeaChecksum(frame.substr(1, star - 1));
  if (reported_checksum != nullptr)
  {
    *reported_checksum = reported;
  }
  if (computed_checksum != nullptr)
  {
    *computed_checksum = computed;
  }

  return reported == computed ? ChecksumStatus::kValid : ChecksumStatus::kInvalid;
}

}  // namespace universal_gnss_protocols
