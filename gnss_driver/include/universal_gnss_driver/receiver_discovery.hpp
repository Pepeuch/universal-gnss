#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace universal_gnss_driver
{

enum class ReceiverTransportType : std::uint8_t
{
  kSerial = 0,
};

enum class ReceiverPortSource : std::uint8_t
{
  kSerialById = 0,
  kTtyAcm = 1,
  kTtyUsb = 2,
  kPlatformUart = 3,
  kExplicitPath = 4,
};

enum class ReceiverDetectedFamily : std::uint8_t
{
  kUnknown = 0,
  kUblox = 1,
  kUnicore = 2,
  kNmea = 3,
};

enum class ReceiverProbeConfidence : std::uint8_t
{
  kNone = 0,
  kLow = 1,
  kMedium = 2,
  kHigh = 3,
};

struct ReceiverPortCandidate
{
  std::string path{};
  std::optional<std::string> stable_id{};
  ReceiverTransportType transport_type{ReceiverTransportType::kSerial};
  ReceiverPortSource source{ReceiverPortSource::kTtyUsb};
};

struct ReceiverDiscoveryPaths
{
  std::string serial_by_id_dir{"/dev/serial/by-id"};
  std::string dev_dir{"/dev"};
};

struct ReceiverProbeConfig
{
  std::vector<std::uint32_t> baud_candidates{
      921600u, 460800u, 230400u, 115200u, 38400u, 9600u};
  int confidence_threshold_score{100};
  std::uint32_t read_timeout_ms{250u};
  std::size_t max_probe_bytes{4096u};
  bool allow_generic_nmea_fallback{true};
  bool include_platform_uarts{false};
  std::vector<std::string> platform_uart_paths{"/dev/serial0", "/dev/serial1"};
  std::vector<std::string> platform_uart_prefixes{"ttyAMA", "ttyS", "ttyTHS"};
};

struct ReceiverProbeEvidence
{
  std::size_t ubx_frames_seen{0u};
  std::size_t unicore_ascii_seen{0u};
  std::size_t unicore_binary_seen{0u};
  std::size_t nmea_sentences_seen{0u};
  std::size_t rtcm_frames_seen{0u};
  std::size_t mavlink_heartbeats_seen{0u};
  std::size_t random_ascii_bytes_seen{0u};
  std::size_t bytes_read{0u};
};

// Receiver-incarnation metadata observed during one probe. These values are
// intentionally separate from GNSS runtime state and may be unavailable.
struct ReceiverIdentityMetadata
{
  std::optional<std::string> receiver_identity{};
  std::optional<std::string> model{};
  std::optional<std::string> firmware_version{};
};

struct ReceiverProbeResult
{
  std::string path{};
  std::optional<std::string> stable_id{};
  ReceiverTransportType transport_type{ReceiverTransportType::kSerial};
  ReceiverPortSource source{ReceiverPortSource::kExplicitPath};
  std::optional<std::uint32_t> selected_baud{};
  ReceiverDetectedFamily detected_family{ReceiverDetectedFamily::kUnknown};
  ReceiverProbeConfidence confidence{ReceiverProbeConfidence::kNone};
  int discovery_score{0};
  ReceiverProbeEvidence evidence{};
  ReceiverIdentityMetadata identity{};
  std::string reason{};
  std::string note{};
};

std::vector<ReceiverPortCandidate> DiscoverSerialPorts(
    const ReceiverProbeConfig& config,
    const ReceiverDiscoveryPaths& paths = {});

std::vector<ReceiverPortCandidate> DiscoverSerialPorts(
    const ReceiverDiscoveryPaths& paths = {});

ReceiverPortCandidate MakeExplicitReceiverPortCandidate(
    const std::string& path,
    const ReceiverDiscoveryPaths& paths = {});

ReceiverProbeResult AnalyzeReceiverProbeBytes(const ReceiverPortCandidate& candidate,
                                              std::uint32_t baud_rate,
                                              const std::vector<std::uint8_t>& bytes,
                                              const ReceiverProbeConfig& config = {});

ReceiverProbeResult ProbeReceiverPort(const ReceiverPortCandidate& candidate,
                                      const ReceiverProbeConfig& config = {});

std::vector<ReceiverProbeResult> DiscoverReceivers(
    const ReceiverProbeConfig& config = {},
    const std::optional<std::string>& explicit_path = std::nullopt,
    const ReceiverDiscoveryPaths& paths = {});

std::vector<ReceiverProbeResult> SortReceiverProbeResults(
    std::vector<ReceiverProbeResult> results);

const char* ToString(ReceiverTransportType transport_type);
const char* ToString(ReceiverPortSource source);
const char* ToString(ReceiverDetectedFamily family);
const char* ToString(ReceiverProbeConfidence confidence);

}  // namespace universal_gnss_driver
