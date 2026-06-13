#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "universal_gnss/gnss_runtime_aggregator.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"

namespace universal_gnss_driver
{

struct UbloxSessionConfig
{
  std::size_t max_nmea_sentence_length_bytes{512u};
  std::size_t max_ubx_frame_length_bytes{4096u};
  std::size_t max_rtcm_frame_length_bytes{1029u};
  bool enable_nmea_runtime_updates{true};
};

struct UbloxSessionMetrics
{
  std::size_t bytes_seen{0u};
  std::size_t ubx_frames_seen{0u};
  std::size_t nmea_sentences_seen{0u};
  std::size_t rtcm_frames_seen{0u};
  std::size_t frames_parsed{0u};
  std::size_t frames_rejected{0u};
  std::size_t runtime_observations{0u};
  std::size_t runtime_updates{0u};
  std::size_t unknown_frames{0u};
  std::size_t malformed_frames{0u};
  std::size_t receiver_rtcm_messages_seen{0u};
  std::size_t receiver_rtcm_messages_used{0u};
  std::size_t receiver_rtcm_messages_not_used{0u};
  std::size_t receiver_rtcm_crc_failed{0u};
  std::optional<std::uint16_t> last_receiver_rtcm_message_type{};
  std::map<std::uint16_t, std::size_t> rtcm_message_type_counts{};
};

class UbloxSession
{
public:
  struct BufferedByte
  {
    std::uint8_t value{0u};
    std::optional<std::int64_t> timestamp_ns{};
  };

  explicit UbloxSession(UbloxSessionConfig config = {});

  void FeedBytes(const std::uint8_t* data,
                 std::size_t size,
                 std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void FeedBytes(const std::vector<std::uint8_t>& bytes,
                 std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void FeedString(std::string_view text, std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void Finalize();

  void Reset();

  const universal_gnss::GnssRuntimeState& current_state() const;

  const UbloxSessionMetrics& metrics() const;

  const UbloxSessionConfig& config() const;

private:
  void ProcessBufferedData(bool finalizing);
  bool ConsumeNmeaAtOffset(std::size_t start_offset,
                           bool finalizing,
                           std::size_t& next_offset,
                           bool& keep_tail);
  bool ConsumeUbxAtOffset(std::size_t start_offset,
                          bool finalizing,
                          std::size_t& next_offset,
                          bool& keep_tail);
  bool ConsumeRtcmAtOffset(std::size_t start_offset,
                           bool finalizing,
                           std::size_t& next_offset,
                           bool& keep_tail);

  void RouteNmeaSentence(const universal_gnss_protocols::NmeaSentence& sentence);
  void RouteUbxFrame(const universal_gnss_protocols::UbxFrame& frame);
  void RouteRtcmFrame(const universal_gnss_protocols::RtcmFrame& frame);

  UbloxSessionConfig config_{};
  std::vector<BufferedByte> buffer_{};
  universal_gnss::GnssRuntimeAggregator aggregator_{};
  UbloxSessionMetrics metrics_{};
};

}  // namespace universal_gnss_driver
