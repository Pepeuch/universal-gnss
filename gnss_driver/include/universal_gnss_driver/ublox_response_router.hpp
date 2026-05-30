#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

#include "universal_gnss_driver/receiver_command_response.hpp"
#include "universal_gnss_driver/ubx_command_response_mapper.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"

namespace universal_gnss_driver
{

struct UbloxRoutedResponse
{
  ReceiverCommandResponse response{};
  std::optional<UbxMessageIdentity> ubx_target{};
};

struct UbloxResponseRouterMetrics
{
  std::size_t frames_seen{0u};
  std::size_t ack_frames_seen{0u};
  std::size_t nak_frames_seen{0u};
  std::size_t responses_generated{0u};
  std::size_t ignored_frames{0u};
  std::size_t malformed_frames{0u};
};

class UbloxResponseRouter
{
public:
  bool ProcessUbxFrame(const universal_gnss_protocols::UbxFrame& frame);

  bool TryGetResponse(UbloxRoutedResponse& response) const;

  bool PopResponse(UbloxRoutedResponse& response);

  void Reset();

  std::size_t pending_response_count() const;

  const UbloxResponseRouterMetrics& metrics() const;

private:
  std::deque<UbloxRoutedResponse> queued_responses_{};
  UbloxResponseRouterMetrics metrics_{};
};

}  // namespace universal_gnss_driver
