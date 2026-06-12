#include "universal_gnss_driver/unicore_response_router.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <utility>

namespace universal_gnss_driver
{

namespace
{

constexpr std::array<const char*, 22u> kIgnoredTelemetryPrefixes{
    "#BESTNAVA",
    "#BESTNAVB",
    "#PVTSLNA",
    "#PVTSLNB",
    "#RTKSTATUSA",
    "#RTKSTATUSB",
    "#RTCMSTATUSA",
    "#RTCMSTATUSB",
    "#BESTSATA",
    "#BESTSATB",
    "#SATSINFOA",
    "#SATSINFOB",
    "#AGCA",
    "#AGCB",
    "#HWSTATUSA",
    "#HWSTATUSB",
    "#JAMSTATUSA",
    "#JAMSTATUSB",
    "#FREQJAMSTATUSA",
    "#FREQJAMSTATUSB",
    "#OBSVMCMPA",
    "#OBSVMCMPB",
};

constexpr std::array<const char*, 12u> kIgnoredNmeaPrefixes{
    "$GPGGA",
    "$GNGGA",
    "$GPGSV",
    "$GLGSV",
    "$GAGSV",
    "$GBGSV",
    "$GPGST",
    "$GNGST",
    "$GNHPR",
    "$GPHPR",
    "$GNHPR2",
    "$GPHPR2",
};

constexpr std::array<const char*, 5u> kNegativeResponseHints{
    "unsupported",
    "parsing failed",
    "grammar error",
    "response can't found device",
    "response cant found device",
};

bool StartsWith(const std::string_view text, const std::string_view prefix)
{
  return text.size() >= prefix.size() &&
         text.compare(0u, prefix.size(), prefix) == 0;
}

std::string TrimLineEnding(std::string_view line)
{
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
  {
    line.remove_suffix(1u);
  }
  return std::string(line);
}

std::string ToLowerAscii(std::string_view text)
{
  std::string lower;
  lower.reserve(text.size());
  for (const unsigned char c : text)
  {
    lower.push_back(static_cast<char>(std::tolower(c)));
  }
  return lower;
}

std::string_view TrimLeadingResponseNoise(std::string_view line)
{
  std::optional<std::size_t> best_pos{};
  auto update_best = [&](const std::size_t pos) {
    if (pos == std::string_view::npos || pos == 0u)
    {
      return;
    }
    if (!best_pos.has_value() || pos < *best_pos)
    {
      best_pos = pos;
    }
  };

  update_best(line.find("$command,"));
  update_best(line.find("<OK"));
  update_best(line.find("#VERSIONA"));

  const std::string lower = ToLowerAscii(line);
  for (const char* hint : kNegativeResponseHints)
  {
    update_best(lower.find(hint));
  }

  return best_pos.has_value() ? line.substr(*best_pos) : line;
}

bool IsPrintableAsciiText(std::string_view text)
{
  for (const unsigned char c : text)
  {
    if (c == '\t')
    {
      continue;
    }

    if (c < 0x20u || c > 0x7Eu)
    {
      return false;
    }
  }

  return true;
}

bool MatchesOkResponse(std::string_view line)
{
  return line == "<OK" || StartsWith(line, "<OK ") || StartsWith(line, "<OK,");
}

bool MatchesCommandAcceptedResponse(std::string_view line)
{
  if (!StartsWith(line, "$command,"))
  {
    return false;
  }

  const std::string lower = ToLowerAscii(line);
  return lower.find(",response: ok") != std::string::npos;
}

bool MatchesVersionResponse(std::string_view line)
{
  return StartsWith(line, "#VERSIONA");
}

bool MatchesNegativeResponse(std::string_view line)
{
  const std::string lower = ToLowerAscii(line);
  return std::any_of(
      kNegativeResponseHints.begin(),
      kNegativeResponseHints.end(),
      [&lower](const char* hint) { return lower.find(hint) != std::string::npos; });
}

bool IsIgnoredTelemetryLine(std::string_view line)
{
  return std::any_of(
             kIgnoredTelemetryPrefixes.begin(),
             kIgnoredTelemetryPrefixes.end(),
             [line](const char* prefix) { return StartsWith(line, prefix); }) ||
         std::any_of(
             kIgnoredNmeaPrefixes.begin(),
             kIgnoredNmeaPrefixes.end(),
             [line](const char* prefix) { return StartsWith(line, prefix); });
}

ReceiverCommandResponse BuildResponse(
    const ReceiverCommandResponseKind kind,
    const std::optional<ReceiverCommandTimestampNs> timestamp_ns,
    const std::string& message)
{
  ReceiverCommandResponse response;
  response.kind = kind;
  response.timestamp_ns = timestamp_ns;
  response.message = message;
  return response;
}

}  // namespace

bool UnicoreResponseRouter::ProcessLine(
    std::string_view line,
    std::optional<ReceiverCommandTimestampNs> timestamp_ns)
{
  ++metrics_.lines_seen;

  const std::string trimmed = TrimLineEnding(line);
  const std::string normalized =
      std::string(TrimLeadingResponseNoise(std::string_view(trimmed)));
  if (normalized.empty() || !IsPrintableAsciiText(normalized))
  {
    ++metrics_.malformed_lines;
    return false;
  }

  if (MatchesNegativeResponse(normalized))
  {
    queued_responses_.push_back(
        BuildResponse(ReceiverCommandResponseKind::kTextError, timestamp_ns, normalized));
    ++metrics_.error_responses_seen;
    ++metrics_.responses_generated;
    return true;
  }

  if (MatchesOkResponse(normalized) || MatchesCommandAcceptedResponse(normalized) ||
      MatchesVersionResponse(normalized))
  {
    queued_responses_.push_back(
        BuildResponse(ReceiverCommandResponseKind::kTextOk, timestamp_ns, normalized));
    ++metrics_.ok_responses_seen;
    ++metrics_.responses_generated;
    return true;
  }

  if (StartsWith(normalized, "$command,") || StartsWith(normalized, "<"))
  {
    ++metrics_.malformed_lines;
    return false;
  }

  if (IsIgnoredTelemetryLine(normalized) || StartsWith(normalized, "#"))
  {
    ++metrics_.ignored_lines;
    return false;
  }

  ++metrics_.ignored_lines;
  return false;
}

void UnicoreResponseRouter::FeedBytes(
    std::string_view data,
    std::optional<ReceiverCommandTimestampNs> timestamp_ns)
{
  for (const char c : data)
  {
    if (buffered_line_.empty() && c != '\n')
    {
      buffered_line_timestamp_ns_ = timestamp_ns;
    }

    if (c == '\n')
    {
      const auto line_timestamp =
          buffered_line_timestamp_ns_.has_value() ? buffered_line_timestamp_ns_ : timestamp_ns;
      ProcessLine(buffered_line_, line_timestamp);
      buffered_line_.clear();
      buffered_line_timestamp_ns_.reset();
      continue;
    }

    buffered_line_.push_back(c);
  }
}

bool UnicoreResponseRouter::TryGetResponse(ReceiverCommandResponse& response) const
{
  if (queued_responses_.empty())
  {
    return false;
  }

  response = queued_responses_.front();
  return true;
}

bool UnicoreResponseRouter::PopResponse(ReceiverCommandResponse& response)
{
  if (queued_responses_.empty())
  {
    return false;
  }

  response = std::move(queued_responses_.front());
  queued_responses_.pop_front();
  return true;
}

void UnicoreResponseRouter::Reset()
{
  queued_responses_.clear();
  buffered_line_.clear();
  buffered_line_timestamp_ns_.reset();
  metrics_ = UnicoreResponseRouterMetrics{};
}

std::size_t UnicoreResponseRouter::pending_response_count() const
{
  return queued_responses_.size();
}

const UnicoreResponseRouterMetrics& UnicoreResponseRouter::metrics() const
{
  return metrics_;
}

}  // namespace universal_gnss_driver
