#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

#include "universal_gnss_driver/receiver_command_response.hpp"

namespace universal_gnss_driver
{

struct UnicoreResponseRouterMetrics
{
  std::size_t lines_seen{0u};
  std::size_t ok_responses_seen{0u};
  std::size_t error_responses_seen{0u};
  std::size_t responses_generated{0u};
  std::size_t ignored_lines{0u};
  std::size_t malformed_lines{0u};
};

class UnicoreResponseRouter
{
public:
  bool ProcessLine(
      std::string_view line,
      std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt);

  void FeedBytes(
      std::string_view data,
      std::optional<ReceiverCommandTimestampNs> timestamp_ns = std::nullopt);

  bool TryGetResponse(ReceiverCommandResponse& response) const;

  bool PopResponse(ReceiverCommandResponse& response);

  void Reset();

  std::size_t pending_response_count() const;

  const UnicoreResponseRouterMetrics& metrics() const;

private:
  std::deque<ReceiverCommandResponse> queued_responses_{};
  std::string buffered_line_{};
  std::optional<ReceiverCommandTimestampNs> buffered_line_timestamp_ns_{};
  UnicoreResponseRouterMetrics metrics_{};
};

}  // namespace universal_gnss_driver
