#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "universal_gnss/gnss_runtime_aggregator.hpp"
#include "universal_gnss_protocols/nmea_framer.hpp"

namespace universal_gnss_driver
{

struct NmeaSessionConfig
{
  std::size_t max_sentence_length_bytes{512u};
  bool enable_runtime_updates{true};
};

struct NmeaSessionMetrics
{
  std::size_t bytes_seen{0u};
  std::size_t sentences_seen{0u};
  std::size_t records_parsed{0u};
  std::size_t records_rejected{0u};
  std::size_t runtime_observations{0u};
  std::size_t runtime_updates{0u};
  std::size_t semantic_only_records{0u};
  std::size_t unknown_sentences{0u};
  std::size_t malformed_sentences{0u};
};

class NmeaSession
{
public:
  explicit NmeaSession(NmeaSessionConfig config = {});

  void FeedBytes(const std::uint8_t* data,
                 std::size_t size,
                 std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void FeedBytes(const std::vector<std::uint8_t>& bytes,
                 std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void FeedString(std::string_view text, std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void Finalize();

  void Reset();

  const universal_gnss::GnssRuntimeState& current_state() const;

  const NmeaSessionMetrics& metrics() const;

  const NmeaSessionConfig& config() const;

private:
  void HandleFramerResult(
      const universal_gnss_protocols::ParserResult<universal_gnss_protocols::NmeaSentence>& result);
  void HandleSentence(const universal_gnss_protocols::NmeaSentence& sentence);

  NmeaSessionConfig config_{};
  universal_gnss_protocols::NmeaSentenceFramer framer_;
  universal_gnss::GnssRuntimeAggregator aggregator_{};
  NmeaSessionMetrics metrics_{};
};

}  // namespace universal_gnss_driver
