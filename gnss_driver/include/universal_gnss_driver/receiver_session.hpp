#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "universal_gnss_driver/nmea_session.hpp"
#include "universal_gnss_driver/ublox_session.hpp"
#include "universal_gnss_driver/unicore_session.hpp"

namespace universal_gnss_driver
{

enum class ReceiverSessionKind : std::uint8_t
{
  kAutoDetect = 0,
  kUblox = 1,
  kUnicore = 2,
  kNmea = 3,
};

struct ReceiverSessionConfig
{
  ReceiverSessionKind kind{ReceiverSessionKind::kAutoDetect};
  std::size_t max_auto_detect_buffer_bytes{4096u};
  bool allow_generic_nmea_auto_detect{false};
  NmeaSessionConfig nmea{};
  UbloxSessionConfig ublox{};
  UnicoreSessionConfig unicore{};
};

struct ReceiverSessionMetrics
{
  std::size_t bytes_seen{0u};
  std::optional<ReceiverSessionKind> selected_session_kind{};
  bool selection_locked{false};
  std::size_t sessions_switched{0u};
  std::size_t runtime_observations{0u};
  std::size_t runtime_updates{0u};
  std::size_t malformed_records{0u};
  std::size_t rejected_records{0u};
  std::size_t parser_anomalies{0u};
  std::size_t unknown_records{0u};
};

class ReceiverSession
{
public:
  struct BufferedByte
  {
    std::uint8_t value{0u};
    std::optional<std::int64_t> timestamp_ns{};
  };

  explicit ReceiverSession(ReceiverSessionConfig config = {});

  void FeedBytes(const std::uint8_t* data,
                 std::size_t size,
                 std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void FeedBytes(const std::vector<std::uint8_t>& bytes,
                 std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void FeedString(std::string_view text, std::optional<std::int64_t> timestamp_ns = std::nullopt);

  void Finalize();

  void Reset();

  const universal_gnss::GnssRuntimeState& current_state() const;

  const ReceiverSessionMetrics& metrics() const;

  const ReceiverSessionConfig& config() const;

  const UbloxSessionMetrics& ublox_metrics() const;

  const UnicoreSessionMetrics& unicore_metrics() const;

  const NmeaSessionMetrics& nmea_metrics() const;

private:
  void InitializeSelectionFromConfig();
  void RouteToSelectedSession(const std::uint8_t* data,
                              std::size_t size,
                              std::optional<std::int64_t> timestamp_ns);
  void RouteBufferedBytesToSelectedSession();
  void AppendPendingBytes(const std::uint8_t* data,
                          std::size_t size,
                          std::optional<std::int64_t> timestamp_ns);
  void TrimPendingBytesIfNeeded();
  void TrySelectSessionFromPendingBytes();
  void SelectSession(ReceiverSessionKind kind);
  void RefreshMetricsFromSelectedSession();

  ReceiverSessionConfig config_{};
  ReceiverSessionMetrics metrics_{};
  NmeaSession nmea_session_;
  UbloxSession ublox_session_;
  UnicoreSession unicore_session_;
  std::vector<BufferedByte> pending_auto_detect_bytes_{};
  universal_gnss::GnssRuntimeState empty_state_{};
};

const char* ToString(ReceiverSessionKind kind);

}  // namespace universal_gnss_driver
