#pragma once

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/protocol_type.hpp"

namespace universal_gnss_tools
{

struct GnssReplayEvent
{
  std::size_t event_index{0};
  std::size_t byte_offset{0};
  std::size_t length_bytes{0};
  universal_gnss_protocols::ProtocolType protocol{
      universal_gnss_protocols::ProtocolType::kUnknown};
  universal_gnss_protocols::ChecksumStatus checksum_status{
      universal_gnss_protocols::ChecksumStatus::kNotChecked};

  std::string identity{};
  std::string classification{};
  bool produced_runtime_update{false};
  universal_gnss::GnssRuntimeState state_after_event{};
};

struct GnssReplaySummary
{
  std::size_t total_bytes_read{0};
  std::size_t recognized_records{0};
  std::size_t valid_records{0};
  std::size_t invalid_records{0};
  std::size_t malformed_events{0};
  std::size_t truncated_records{0};
  std::size_t noise_bytes{0};
  std::size_t noise_spans{0};
  std::size_t runtime_updates{0};

  std::map<std::string, std::size_t> counts_by_protocol{};
  std::map<std::string, std::size_t> counts_by_nmea_sentence_type{};
  std::map<std::string, std::size_t> counts_by_ubx_message{};
  std::map<std::string, std::size_t> counts_by_unicore_message{};
  std::map<std::uint16_t, std::size_t> counts_by_rtcm_message_type{};
};

struct GnssReplayResult
{
  std::vector<GnssReplayEvent> events{};
  GnssReplaySummary summary{};
  universal_gnss::GnssRuntimeState final_state{};
};

enum class GnssReplayTimingMode : std::uint8_t
{
  kFast = 0,
  kWallTime = 1,
};

struct GnssReplayTimingConfig
{
  GnssReplayTimingMode mode{GnssReplayTimingMode::kFast};
  double speed{1.0};
  std::chrono::milliseconds fallback_step{100};
};

struct GnssReplayTimingStep
{
  std::size_t event_index{0};
  std::chrono::nanoseconds delay_before_event{0};
};

std::vector<GnssReplayTimingStep> BuildGnssReplayTimingPlan(
    const GnssReplayResult& result,
    const GnssReplayTimingConfig& config);

GnssReplayResult ReplayGnssBytes(
    const std::vector<std::uint8_t>& bytes,
    bool include_events = true);

GnssReplayResult ReplayGnssStream(
    std::istream& input,
    bool include_events = true);

std::string FormatGnssReplayText(
    const GnssReplayResult& result,
    bool summary_only = false);

std::string FormatGnssReplayEventText(const GnssReplayEvent& event);

std::string FormatGnssReplayJson(
    const GnssReplayResult& result,
    bool summary_only = false);

}  // namespace universal_gnss_tools
