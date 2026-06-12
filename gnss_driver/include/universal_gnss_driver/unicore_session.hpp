#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "universal_gnss/gnss_runtime_aggregator.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_protocols/unicore_framer.hpp"

namespace universal_gnss_driver
{

struct UnicoreSessionConfig
{
  std::size_t max_frame_length_bytes{2048u};
  std::size_t max_binary_frame_length_bytes{65536u};
};

struct UnicoreSessionMetrics
{
  std::size_t bytes_seen{0u};
  std::size_t lines_seen{0u};
  std::size_t ascii_records_seen{0u};
  std::size_t binary_frames_seen{0u};
  std::size_t records_parsed{0u};
  std::size_t records_rejected{0u};
  std::size_t runtime_updates{0u};
  std::size_t unknown_records{0u};
  std::size_t malformed_lines{0u};
  std::size_t malformed_frames{0u};
  std::size_t receiver_rtcm_status_messages_seen{0u};
  std::uint32_t receiver_rtcm_status_message_count{0u};
  std::optional<std::uint32_t> receiver_last_rtcm_message_type{};
  std::optional<std::uint32_t> receiver_last_rtcm_base_station_id{};
  std::optional<std::uint32_t> receiver_last_rtcm_satellites_in_message{};
};

class UnicoreSession
{
public:
  explicit UnicoreSession(UnicoreSessionConfig config = {});

  void FeedBytes(const std::uint8_t* data,
                 std::size_t size,
                 std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void FeedBytes(const std::vector<std::uint8_t>& bytes,
                 std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void FeedString(std::string_view text,
                  std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void Finalize();

  void Reset();

  const universal_gnss::GnssRuntimeState& current_state() const;

  const UnicoreSessionMetrics& metrics() const;

  const UnicoreSessionConfig& config() const;

private:
  enum class ActiveFramer
  {
    kIdle = 0,
    kAscii,
    kBinary,
  };

  void FeedByte(std::uint8_t byte, std::optional<std::int64_t> timestamp_ns);
  bool ShouldSuppressStartupAsciiMalformed();
  bool ShouldSuppressStartupBinaryMalformed();
  bool RouteFinished(universal_gnss_protocols::ParserStatus status) const;
  bool ShouldRetryAsAscii(
      std::uint8_t byte,
      universal_gnss_protocols::ParserStatus status,
      ActiveFramer active_framer) const;
  static bool IsAsciiSyncByte(std::uint8_t byte);
  static bool IsBinarySyncByte(std::uint8_t byte);
  void HandleFramerResult(
      const universal_gnss_protocols::ParserResult<universal_gnss_protocols::UnicoreFrame>&
          result);
  void HandleBinaryFramerResult(
      const universal_gnss_protocols::ParserResult<universal_gnss_protocols::UnicoreBinaryFrame>&
          result);
  void HandleFrame(const universal_gnss_protocols::UnicoreFrame& frame);
  void HandleBinaryFrame(const universal_gnss_protocols::UnicoreBinaryFrame& frame);

  UnicoreSessionConfig config_{};
  universal_gnss_protocols::UnicoreFrameFramer framer_;
  universal_gnss_protocols::UnicoreBinaryFrameFramer binary_framer_;
  universal_gnss::GnssRuntimeAggregator aggregator_{};
  UnicoreSessionMetrics metrics_{};
  bool ascii_seen_valid_record_{false};
  bool binary_seen_valid_frame_{false};
  bool ascii_startup_malformed_suppressed_{false};
  bool binary_startup_malformed_suppressed_{false};
  bool finalizing_{false};
  ActiveFramer active_framer_{ActiveFramer::kIdle};
};

}  // namespace universal_gnss_driver
