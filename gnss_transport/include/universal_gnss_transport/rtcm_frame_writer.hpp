#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "universal_gnss_transport/byte_stream.hpp"

namespace universal_gnss_transport {

// Bounded FIFO for complete RTCM frames. A partially written head remains at
// the front until it completes or the owning receiver incarnation abandons it.
class RtcmFrameWriter
{
public:
  static constexpr std::size_t kDefaultCapacity{50u};

  enum class FlushResult : std::uint8_t
  {
    kDrained,
    kBlocked,
    kFailed,
  };

  struct Frame
  {
    std::vector<std::uint8_t> data{};
    std::uint16_t message_type{0u};
  };

  struct FlushOutcome
  {
    FlushResult result{FlushResult::kDrained};
    std::size_t bytes_written{0u};
    std::size_t frames_written{0u};
    std::optional<std::uint16_t> last_message_type{};
    TransportStatus status{TransportStatus::kOk};
    TransportError error{TransportError::kNone};
  };

  explicit RtcmFrameWriter(std::size_t capacity = kDefaultCapacity);
  bool Enqueue(Frame frame);
  FlushOutcome Flush(ByteSink& sink);
  void Abandon();
  std::size_t size() const;
  std::size_t capacity() const;
  bool empty() const;

private:
  struct PendingFrame
  {
    Frame frame{};
    std::size_t offset{0u};
  };

  std::size_t capacity_;
  std::deque<PendingFrame> pending_{};
};

} // namespace universal_gnss_transport
