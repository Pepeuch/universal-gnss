#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss/gnss_runtime_aggregator.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
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
  std::size_t runtime_observations{0u};
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

struct UnicoreNmeaGsvTalkerState
{
  static constexpr std::size_t kMaxMessages = 32u;

  std::string talker{};
  std::optional<std::int64_t> last_timestamp_ns{};
  std::uint8_t total_messages{0u};
  std::uint16_t satellites_in_view{0u};
  std::uint16_t tracked_satellites{0u};
  std::array<bool, kMaxMessages> seen_messages{};
  float cn0_sum{0.0f};
  std::size_t cn0_count{0u};
  float cn0_max{0.0f};
};

struct UnicoreBufferedByte
{
  std::uint8_t value{0u};
  std::optional<std::int64_t> timestamp_ns{};
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

  void FeedString(std::string_view text, std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void Finalize();

  void Reset();

  const universal_gnss::GnssRuntimeState& current_state() const;

  const UnicoreSessionMetrics& metrics() const;

  const UnicoreSessionConfig& config() const;

private:
  void ProcessBufferedData(bool finalizing);
  bool ConsumeNmeaAtOffset(std::size_t start_offset,
                           bool finalizing,
                           std::size_t& next_offset,
                           bool& keep_tail);
  bool ConsumeAsciiAtOffset(std::size_t start_offset,
                            bool finalizing,
                            std::size_t& next_offset,
                            bool& keep_tail);
  bool ConsumeBinaryAtOffset(std::size_t start_offset,
                             bool finalizing,
                             std::size_t& next_offset,
                             bool& keep_tail);
  bool ShouldSuppressStartupAsciiMalformed();
  bool ShouldSuppressStartupBinaryMalformed();
  void HandleNmeaSentence(const universal_gnss_protocols::NmeaSentence& sentence);
  void HandleFrame(const universal_gnss_protocols::UnicoreFrame& frame);
  void HandleBinaryFrame(const universal_gnss_protocols::UnicoreBinaryFrame& frame);

  UnicoreSessionConfig config_{};
  universal_gnss::GnssRuntimeAggregator aggregator_{};
  UnicoreSessionMetrics metrics_{};
  std::vector<UnicoreBufferedByte> buffer_{};
  bool ascii_seen_valid_record_{false};
  bool binary_seen_valid_frame_{false};
  bool ascii_startup_malformed_suppressed_{false};
  bool binary_startup_malformed_suppressed_{false};
  bool seen_valid_nmea_gga_{false};
  bool seen_valid_nmea_gsv_{false};
  std::optional<std::int64_t> last_nmea_gga_timestamp_ns_{};
  std::optional<std::int64_t> last_nmea_gsv_timestamp_ns_{};
  std::vector<UnicoreNmeaGsvTalkerState> gsv_talker_states_{};
};

}  // namespace universal_gnss_driver
