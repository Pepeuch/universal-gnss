#include "universal_gnss_ros2/receiver_node.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "rtcm_diagnostic_projection.hpp"
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_driver/receiver_session_runner.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include "universal_gnss_protocols/rtcm_framer.hpp"
#include "universal_gnss_ros2/diagnostic_adapter.hpp"
#include "universal_gnss_ros2/gnss_status_adapter.hpp"
#include "universal_gnss_ros2/msg/rtcm_frame.hpp"
#include "universal_gnss_ros2/navsat_fix_adapter.hpp"
#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/posix_serial_transport.hpp"
#include "universal_gnss_transport/tcp_client_transport.hpp"

namespace universal_gnss_ros2
{

namespace
{

enum class ReceiverTransportKind : std::uint8_t
{
  kSerial = 0,
  kTcp = 1,
};

constexpr std::int64_t kMaximumReadChunkSizeBytes = 1024 * 1024;
constexpr double kDefaultRtcmForwardingActivityTimeoutSeconds = 5.0;
constexpr double kDefaultRuntimeObservationFallbackTimeoutSeconds = 10.0;
constexpr double kRuntimeObservationJitterPeriods = 3.0;

struct ReceiverNodeConfig
{
  universal_gnss_driver::ReceiverSessionConfig session{};
  ReceiverTransportKind transport_kind{ReceiverTransportKind::kSerial};
  std::string receiver_family_name{"auto"};
  std::string transport_name{"serial"};
  std::string serial_device{};
  std::uint32_t serial_baud{115200u};
  bool receiver_family_auto{true};
  bool serial_device_auto{false};
  bool serial_baud_auto{false};
  bool discovery_include_platform_uarts{false};
  bool discovery_allow_generic_nmea{false};
  std::uint32_t discovery_timeout_ms{250u};
  std::size_t discovery_max_probe_bytes{4096u};
  std::string tcp_host{};
  std::uint16_t tcp_port{0u};
  double publish_rate_hz{10.0};
  // This declares input-observation cadence, not the node's ROS publication
  // cadence. Zero selects the conservative fallback until a deployment
  // supplies its receiver/profile-specific expectation.
  double expected_runtime_observation_rate_hz{0.0};
  double runtime_observation_fallback_timeout_s{
      kDefaultRuntimeObservationFallbackTimeoutSeconds};
  // Bytes pulled from the transport per receiver tick. The node reads ONE chunk
  // per publish cycle, so effective read throughput is read_chunk_size *
  // publish_rate_hz. The previous fixed 512 B (= 2.56 KB/s at 5 Hz) sits below a
  // busy u-blox F9P's UBX output (~13 KB/s measured), so the OS serial buffer
  // backlogs and published fixes lag by seconds. 64 KB drains the buffer in one
  // read per tick; a single ::read() returns all bytes available up to capacity.
  std::size_t read_chunk_size{65536u};
  // This is correction-stream receipt liveness, not GNSS observation freshness.
  // It is measured from local successful writes so public ROS timestamps never
  // cross clock domains here.
  double rtcm_forwarding_activity_timeout_s{kDefaultRtcmForwardingActivityTimeoutSeconds};
  std::string frame_id{"gnss"};
};

struct ReceiverDiscoveryStatus
{
  bool attempted{false};
  bool succeeded{false};
  std::optional<universal_gnss_driver::ReceiverProbeResult> result{};
  std::optional<std::string> failure_reason{};
};

using SteadyClock = std::chrono::steady_clock;

std::string ToLowerCopy(std::string value)
{
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](const unsigned char ch)
                 {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

bool ParseUnsigned32Text(const std::string& text, std::uint32_t& value)
{
  try
  {
    std::size_t consumed = 0u;
    const auto parsed = std::stoul(text, &consumed, 10);
    if (consumed != text.size() ||
        parsed > static_cast<unsigned long>(std::numeric_limits<std::uint32_t>::max()))
    {
      return false;
    }

    value = static_cast<std::uint32_t>(parsed);
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

bool IsAutoToken(const std::string& value)
{
  return ToLowerCopy(value) == "auto";
}

bool IsDiscoveryFamilyAccepted(const ReceiverNodeConfig& config,
                               const universal_gnss_driver::ReceiverProbeResult& result)
{
  if (result.detected_family == universal_gnss_driver::ReceiverDetectedFamily::kUnknown)
  {
    return false;
  }

  if (config.receiver_family_auto)
  {
    return true;
  }

  if (config.receiver_family_name == "ublox")
  {
    return result.detected_family == universal_gnss_driver::ReceiverDetectedFamily::kUblox;
  }
  if (config.receiver_family_name == "unicore")
  {
    return result.detected_family == universal_gnss_driver::ReceiverDetectedFamily::kUnicore;
  }
  if (config.receiver_family_name == "nmea")
  {
    return result.detected_family == universal_gnss_driver::ReceiverDetectedFamily::kNmea;
  }

  return false;
}

void ApplyReceiverFamily(ReceiverNodeConfig& config, const std::string& family_name)
{
  config.receiver_family_name = ToLowerCopy(family_name);
  config.receiver_family_auto = config.receiver_family_name == "auto";

  if (config.receiver_family_name == "auto")
  {
    config.session.kind = universal_gnss_driver::ReceiverSessionKind::kAutoDetect;
  }
  else if (config.receiver_family_name == "ublox")
  {
    config.session.kind = universal_gnss_driver::ReceiverSessionKind::kUblox;
  }
  else if (config.receiver_family_name == "unicore")
  {
    config.session.kind = universal_gnss_driver::ReceiverSessionKind::kUnicore;
  }
  else if (config.receiver_family_name == "nmea")
  {
    config.session.kind = universal_gnss_driver::ReceiverSessionKind::kNmea;
  }
}

bool ShouldRunSerialDiscovery(const ReceiverNodeConfig& config, const bool using_injected_source)
{
  return !using_injected_source && config.transport_kind == ReceiverTransportKind::kSerial &&
         (config.serial_device_auto || config.serial_baud_auto || config.receiver_family_auto);
}

const char* ToString(const universal_gnss_transport::TransportError error)
{
  using universal_gnss_transport::TransportError;

  switch (error)
  {
    case TransportError::kNone:
      return "none";
    case TransportError::kClosed:
      return "closed";
    case TransportError::kInvalidArgument:
      return "invalid_argument";
    case TransportError::kOverflow:
      return "overflow";
    case TransportError::kConnectFailure:
      return "connect_failure";
    case TransportError::kTimeout:
      return "timeout";
    case TransportError::kReadFailure:
      return "read_failure";
    case TransportError::kWriteFailure:
      return "write_failure";
    case TransportError::kUnsupported:
      return "unsupported";
    case TransportError::kUnknown:
    default:
      return "unknown";
  }
}

universal_gnss::GnssDiagnosticEvent MakeEvent(universal_gnss::GnssDiagnosticSeverity severity,
                                              universal_gnss::GnssDiagnosticCategory category,
                                              std::string code,
                                              std::string message)
{
  universal_gnss::GnssDiagnosticEvent event;
  event.severity = severity;
  event.category = category;
  event.code = std::move(code);
  event.message = std::move(message);
  event.source = "receiver_node";
  return event;
}

diagnostic_msgs::msg::KeyValue MakeKeyValue(std::string key, std::string value)
{
  diagnostic_msgs::msg::KeyValue entry;
  entry.key = std::move(key);
  entry.value = std::move(value);
  return entry;
}

void LogDiagnosticEvent(rclcpp::Node& node, const universal_gnss::GnssDiagnosticEvent& event)
{
  switch (event.severity)
  {
    case universal_gnss::GnssDiagnosticSeverity::kError:
      RCLCPP_ERROR(node.get_logger(), "%s: %s", event.code.c_str(), event.message.c_str());
      break;
    case universal_gnss::GnssDiagnosticSeverity::kWarning:
    case universal_gnss::GnssDiagnosticSeverity::kStale:
      RCLCPP_WARN(node.get_logger(), "%s: %s", event.code.c_str(), event.message.c_str());
      break;
    case universal_gnss::GnssDiagnosticSeverity::kInfo:
      RCLCPP_INFO(node.get_logger(), "%s: %s", event.code.c_str(), event.message.c_str());
      break;
    case universal_gnss::GnssDiagnosticSeverity::kOk:
      RCLCPP_INFO(node.get_logger(), "%s: %s", event.code.c_str(), event.message.c_str());
      break;
    case universal_gnss::GnssDiagnosticSeverity::kUnknown:
    default:
      RCLCPP_WARN(node.get_logger(), "%s: %s", event.code.c_str(), event.message.c_str());
      break;
  }
}

std::int64_t MonotonicNowNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             SteadyClock::now().time_since_epoch())
      .count();
}

std::optional<universal_gnss_protocols::ProtocolTimestampNs> RtcmTimestampFromRosMessage(
    const universal_gnss_ros2::msg::RtcmFrame& message)
{
  if (message.stamp.sec == 0 && message.stamp.nanosec == 0u)
  {
    return std::nullopt;
  }

  return static_cast<universal_gnss_protocols::ProtocolTimestampNs>(message.stamp.sec) *
             1000000000LL +
         static_cast<universal_gnss_protocols::ProtocolTimestampNs>(message.stamp.nanosec);
}

[[noreturn]] void ThrowInvalidParameter(rclcpp::Node& node,
                                        const std::string& parameter_name,
                                        const std::string& message)
{
  const std::string full_message = "Invalid parameter '" + parameter_name + "': " + message;
  RCLCPP_ERROR(node.get_logger(), "%s", full_message.c_str());
  throw std::invalid_argument(full_message);
}

std::chrono::nanoseconds ComputePublishPeriod(double publish_rate_hz)
{
  if (!(publish_rate_hz > 0.0))
  {
    publish_rate_hz = 1.0;
  }

  const auto duration = std::chrono::duration<double>(1.0 / publish_rate_hz);
  const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
  return period.count() > 0 ? period : std::chrono::nanoseconds(1);
}

bool IsPositiveSteadyDurationSeconds(const double seconds)
{
  if (!std::isfinite(seconds) || !(seconds > 0.0))
  {
    return false;
  }

  const auto minimum =
      std::chrono::duration<double>(SteadyClock::duration(1)).count();
  const auto maximum =
      std::chrono::duration<double>(SteadyClock::duration::max()).count();
  return seconds >= minimum && seconds <= maximum;
}

bool HasRtkAvailability(const universal_gnss::GnssRuntimeState& state)
{
  if (state.fix_type == universal_gnss::GnssFixType::kRtkFloat ||
      state.fix_type == universal_gnss::GnssFixType::kRtkFixed)
  {
    return true;
  }

  return universal_gnss::HasValueAvailable(state, universal_gnss::GnssCapability::kRtkMode) &&
         state.rtk_mode.has_value() && *state.rtk_mode != universal_gnss::GnssRtkMode::kNone &&
         *state.rtk_mode != universal_gnss::GnssRtkMode::kUnknown;
}

bool HasCorrectionAvailability(const universal_gnss::GnssRuntimeState& state)
{
  return universal_gnss::HasValueAvailable(state, universal_gnss::GnssCapability::kCorrectionAge) &&
         state.correction_age_s.has_value();
}

bool HasKnownBoolField(const universal_gnss::GnssRuntimeState& state,
                       const universal_gnss::GnssCapability capability,
                       const std::optional<bool>& value)
{
  return universal_gnss::HasValueAvailable(state, capability) && value.has_value() && *value;
}

std::string BuildHardwareId(const ReceiverNodeConfig& config, const bool using_injected_source)
{
  if (using_injected_source)
  {
    return "injected_source";
  }

  if (config.transport_kind == ReceiverTransportKind::kSerial)
  {
    return config.serial_device;
  }

  std::ostringstream stream;
  stream << config.tcp_host << ':' << config.tcp_port;
  return stream.str();
}

std::string BuildDiscoveryEvidenceSummary(
    const universal_gnss_driver::ReceiverProbeEvidence& evidence)
{
  std::ostringstream stream;
  stream << "ubx=" << evidence.ubx_frames_seen << " unicore_ascii=" << evidence.unicore_ascii_seen
         << " unicore_binary=" << evidence.unicore_binary_seen
         << " nmea=" << evidence.nmea_sentences_seen << " rtcm=" << evidence.rtcm_frames_seen
         << " mavlink=" << evidence.mavlink_heartbeats_seen
         << " random_ascii=" << evidence.random_ascii_bytes_seen
         << " bytes=" << evidence.bytes_read;
  return stream.str();
}

ReceiverNode::DiscoveryFunction MakeDefaultDiscoveryFunction()
{
  return [](const universal_gnss_driver::ReceiverProbeConfig& config,
            const std::optional<std::string>& explicit_path,
            const universal_gnss_driver::ReceiverDiscoveryPaths& paths)
  {
    return universal_gnss_driver::DiscoverReceivers(config, explicit_path, paths);
  };
}

bool MaybeRunSerialDiscovery(rclcpp::Node& node,
                             ReceiverNodeConfig& config,
                             const ReceiverNode::DiscoveryFunction& discovery_function,
                             std::vector<universal_gnss::GnssDiagnosticEvent>& events,
                             ReceiverDiscoveryStatus& status)
{
  status.attempted = true;

  universal_gnss_driver::ReceiverProbeConfig probe_config;
  probe_config.read_timeout_ms = config.discovery_timeout_ms;
  probe_config.max_probe_bytes = config.discovery_max_probe_bytes;
  probe_config.allow_generic_nmea_fallback = config.discovery_allow_generic_nmea;
  probe_config.include_platform_uarts = config.discovery_include_platform_uarts;
  if (!config.serial_baud_auto)
  {
    probe_config.baud_candidates = {config.serial_baud};
  }

  const std::optional<std::string> explicit_path =
      config.serial_device_auto ? std::nullopt : std::optional<std::string>{config.serial_device};

  auto results = discovery_function(probe_config, explicit_path, {});
  const auto meets_threshold = [](const universal_gnss_driver::ReceiverProbeResult& result)
  {
    return result.confidence == universal_gnss_driver::ReceiverProbeConfidence::kHigh ||
           result.confidence == universal_gnss_driver::ReceiverProbeConfidence::kMedium;
  };

  const universal_gnss_driver::ReceiverProbeResult* selected = nullptr;
  for (const auto& result : results)
  {
    if (!meets_threshold(result))
    {
      continue;
    }
    if (!IsDiscoveryFamilyAccepted(config, result))
    {
      continue;
    }
    if (result.detected_family == universal_gnss_driver::ReceiverDetectedFamily::kNmea &&
        !config.discovery_allow_generic_nmea)
    {
      continue;
    }
    selected = &result;
    break;
  }

  if (selected == nullptr)
  {
    status.succeeded = false;
    std::ostringstream message;
    message << "No receiver matched discovery criteria";
    if (!results.empty())
    {
      message << " (best candidate path=" << results.front().path
              << ", family=" << universal_gnss_driver::ToString(results.front().detected_family)
              << ", confidence=" << universal_gnss_driver::ToString(results.front().confidence)
              << ", score=" << results.front().discovery_score
              << ", reason=" << results.front().reason << ")";
    }
    status.failure_reason = message.str();
    events.push_back(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                               universal_gnss::GnssDiagnosticCategory::kConfiguration,
                               "receiver_discovery_failed",
                               *status.failure_reason));
    RCLCPP_ERROR(node.get_logger(), "%s", status.failure_reason->c_str());
    return false;
  }

  status.succeeded = true;
  status.result = *selected;

  config.serial_device = selected->path;
  config.serial_device_auto = false;
  if (selected->selected_baud.has_value())
  {
    config.serial_baud = *selected->selected_baud;
  }
  config.serial_baud_auto = false;

  switch (selected->detected_family)
  {
    case universal_gnss_driver::ReceiverDetectedFamily::kUblox:
      ApplyReceiverFamily(config, "ublox");
      break;
    case universal_gnss_driver::ReceiverDetectedFamily::kUnicore:
      ApplyReceiverFamily(config, "unicore");
      break;
    case universal_gnss_driver::ReceiverDetectedFamily::kNmea:
      ApplyReceiverFamily(config, "nmea");
      break;
    case universal_gnss_driver::ReceiverDetectedFamily::kUnknown:
    default:
      break;
  }

  RCLCPP_INFO(node.get_logger(),
              "Receiver discovery selected path=%s baud=%u family=%s confidence=%s score=%d "
              "reason=%s evidence=%s",
              config.serial_device.c_str(),
              config.serial_baud,
              config.receiver_family_name.c_str(),
              universal_gnss_driver::ToString(selected->confidence),
              selected->discovery_score,
              selected->reason.c_str(),
              BuildDiscoveryEvidenceSummary(selected->evidence).c_str());
  return true;
}

ReceiverNodeConfig LoadReceiverNodeConfig(rclcpp::Node& node, const bool using_injected_source)
{
  ReceiverNodeConfig config;

  config.receiver_family_name =
      ToLowerCopy(node.declare_parameter<std::string>("receiver_family", "auto"));
  config.transport_name = ToLowerCopy(node.declare_parameter<std::string>("transport", "serial"));
  config.serial_device = node.declare_parameter<std::string>("serial_device", "");
  rcl_interfaces::msg::ParameterDescriptor serial_baud_descriptor;
  serial_baud_descriptor.dynamic_typing = true;
  node.declare_parameter("serial_baud",
                         rclcpp::ParameterValue(std::string("115200")),
                         serial_baud_descriptor);
  const auto serial_baud_parameter = node.get_parameter("serial_baud");
  config.tcp_host = node.declare_parameter<std::string>("tcp_host", "");
  const auto tcp_port = node.declare_parameter<std::int64_t>("tcp_port", 0);
  config.publish_rate_hz = node.declare_parameter<double>("publish_rate_hz", 10.0);
  config.expected_runtime_observation_rate_hz =
      node.declare_parameter<double>("expected_runtime_observation_rate_hz", 0.0);
  config.runtime_observation_fallback_timeout_s = node.declare_parameter<double>(
      "runtime_observation_fallback_timeout_s", kDefaultRuntimeObservationFallbackTimeoutSeconds);
  const auto read_chunk_size = node.declare_parameter<std::int64_t>("read_chunk_size", 65536);
  config.rtcm_forwarding_activity_timeout_s = node.declare_parameter<double>(
      "rtcm_forwarding_activity_timeout_s", kDefaultRtcmForwardingActivityTimeoutSeconds);
  config.frame_id = node.declare_parameter<std::string>("frame_id", "gnss");
  config.discovery_include_platform_uarts =
      node.declare_parameter<bool>("discovery_include_platform_uarts", false);
  config.discovery_allow_generic_nmea =
      node.declare_parameter<bool>("discovery_allow_generic_nmea", true);
  const auto discovery_timeout_ms =
      node.declare_parameter<std::int64_t>("discovery_timeout_ms", 250);
  const auto discovery_max_probe_bytes =
      node.declare_parameter<std::int64_t>("discovery_max_probe_bytes", 4096);

  ApplyReceiverFamily(config, config.receiver_family_name);
  if (config.receiver_family_name != "auto" && config.receiver_family_name != "ublox" &&
      config.receiver_family_name != "unicore" && config.receiver_family_name != "nmea")
  {
    ThrowInvalidParameter(node, "receiver_family", "expected one of: auto, nmea, ublox, unicore");
  }

  if (config.transport_name == "serial")
  {
    config.transport_kind = ReceiverTransportKind::kSerial;
  }
  else if (config.transport_name == "tcp")
  {
    config.transport_kind = ReceiverTransportKind::kTcp;
  }
  else
  {
    ThrowInvalidParameter(node, "transport", "expected one of: serial, tcp");
  }

  if (serial_baud_parameter.get_type() == rclcpp::ParameterType::PARAMETER_INTEGER)
  {
    const auto serial_baud = serial_baud_parameter.as_int();
    if (serial_baud <= 0 ||
        serial_baud > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
    {
      ThrowInvalidParameter(node, "serial_baud", "must be in the 1..4294967295 range");
    }
    config.serial_baud = static_cast<std::uint32_t>(serial_baud);
  }
  else if (serial_baud_parameter.get_type() == rclcpp::ParameterType::PARAMETER_STRING)
  {
    const auto serial_baud_text = ToLowerCopy(serial_baud_parameter.as_string());
    if (serial_baud_text == "auto")
    {
      config.serial_baud_auto = true;
    }
    else if (!ParseUnsigned32Text(serial_baud_text, config.serial_baud) || config.serial_baud == 0u)
    {
      ThrowInvalidParameter(node,
                            "serial_baud",
                            "expected a positive integer baud rate or the string 'auto'");
    }
  }
  else
  {
    ThrowInvalidParameter(node,
                          "serial_baud",
                          "expected a positive integer baud rate or the string 'auto'");
  }

  if (discovery_timeout_ms <= 0 ||
      discovery_timeout_ms > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
  {
    ThrowInvalidParameter(node, "discovery_timeout_ms", "must be in the 1..4294967295 range");
  }
  config.discovery_timeout_ms = static_cast<std::uint32_t>(discovery_timeout_ms);

  if (discovery_max_probe_bytes <= 0)
  {
    ThrowInvalidParameter(node, "discovery_max_probe_bytes", "must be strictly positive");
  }
  config.discovery_max_probe_bytes = static_cast<std::size_t>(discovery_max_probe_bytes);

  if (!std::isfinite(config.publish_rate_hz) || !(config.publish_rate_hz > 0.0))
  {
    ThrowInvalidParameter(node, "publish_rate_hz", "must be finite and strictly positive");
  }

  if (!std::isfinite(config.expected_runtime_observation_rate_hz) ||
      config.expected_runtime_observation_rate_hz < 0.0)
  {
    ThrowInvalidParameter(node,
                          "expected_runtime_observation_rate_hz",
                          "must be finite and non-negative; zero selects the fallback timeout");
  }

  if (!IsPositiveSteadyDurationSeconds(config.runtime_observation_fallback_timeout_s))
  {
    ThrowInvalidParameter(node,
                          "runtime_observation_fallback_timeout_s",
                          "must be a positive finite duration representable by steady_clock");
  }

  if (config.expected_runtime_observation_rate_hz > 0.0 &&
      !IsPositiveSteadyDurationSeconds(kRuntimeObservationJitterPeriods /
                                       config.expected_runtime_observation_rate_hz))
  {
    ThrowInvalidParameter(node,
                          "expected_runtime_observation_rate_hz",
                          "produces a freshness timeout not representable by steady_clock");
  }

  if (!std::isfinite(config.rtcm_forwarding_activity_timeout_s) ||
      !(config.rtcm_forwarding_activity_timeout_s > 0.0))
  {
    ThrowInvalidParameter(node,
                          "rtcm_forwarding_activity_timeout_s",
                          "must be finite and strictly positive");
  }

  if (read_chunk_size <= 0 || read_chunk_size > kMaximumReadChunkSizeBytes)
  {
    ThrowInvalidParameter(node, "read_chunk_size", "must be in the 1..1048576 range");
  }
  config.read_chunk_size = static_cast<std::size_t>(read_chunk_size);

  if (config.frame_id.empty())
  {
    ThrowInvalidParameter(node, "frame_id", "must not be empty");
  }

  config.serial_device_auto = IsAutoToken(config.serial_device);

  if (!using_injected_source && config.transport_kind == ReceiverTransportKind::kSerial &&
      config.serial_device.empty())
  {
    ThrowInvalidParameter(node,
                          "serial_device",
                          "must be set to a device path or 'auto' when transport=serial");
  }

  if (config.transport_kind == ReceiverTransportKind::kTcp)
  {
    if (tcp_port <= 0 ||
        tcp_port > static_cast<std::int64_t>(std::numeric_limits<std::uint16_t>::max()))
    {
      ThrowInvalidParameter(node, "tcp_port", "must be in the 1..65535 range when transport=tcp");
    }
    config.tcp_port = static_cast<std::uint16_t>(tcp_port);

    if (!using_injected_source && config.tcp_host.empty())
    {
      ThrowInvalidParameter(node, "tcp_host", "must be set when transport=tcp");
    }
  }
  else
  {
    config.tcp_port = 0u;
  }

  return config;
}

std::unique_ptr<universal_gnss_transport::ByteSource> CreateTransportSource(
    const ReceiverNodeConfig& config, std::vector<universal_gnss::GnssDiagnosticEvent>& events)
{
  using universal_gnss_transport::ByteSource;
  using universal_gnss_transport::TransportError;

  if (config.transport_kind == ReceiverTransportKind::kSerial)
  {
#if defined(UNIVERSAL_GNSS_TRANSPORT_HAS_POSIX_SERIAL)
    if (config.serial_device.empty() || config.serial_device_auto)
    {
      events.push_back(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                 universal_gnss::GnssDiagnosticCategory::kConfiguration,
                                 "serial_device_missing",
                                 "transport=serial requires serial_device"));
      return nullptr;
    }

    if (config.serial_baud_auto)
    {
      events.push_back(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                 universal_gnss::GnssDiagnosticCategory::kConfiguration,
                                 "serial_baud_missing",
                                 "transport=serial requires a resolved serial_baud"));
      return nullptr;
    }

    auto transport = std::make_unique<universal_gnss_transport::PosixSerialTransport>();
    universal_gnss_transport::PosixSerialConfig serial_config;
    serial_config.device_path = config.serial_device;
    serial_config.baud_rate = config.serial_baud;
    serial_config.nonblocking = true;
    serial_config.read_timeout_ms = 0u;
    const TransportError error = transport->Open(serial_config);
    if (error != TransportError::kNone)
    {
      events.push_back(
          MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                    universal_gnss::GnssDiagnosticCategory::kTransport,
                    "serial_open_failed",
                    "Failed to open serial transport: " + std::string(ToString(error))));
      return nullptr;
    }

    return std::unique_ptr<ByteSource>(transport.release());
#else
    events.push_back(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                               universal_gnss::GnssDiagnosticCategory::kTransport,
                               "serial_transport_unavailable",
                               "POSIX serial transport is unavailable on this platform"));
    return nullptr;
#endif
  }

#if defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  if (config.tcp_host.empty() || config.tcp_port == 0u)
  {
    events.push_back(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                               universal_gnss::GnssDiagnosticCategory::kConfiguration,
                               "tcp_endpoint_missing",
                               "transport=tcp requires tcp_host and tcp_port"));
    return nullptr;
  }

  auto transport = std::make_unique<universal_gnss_transport::TcpClientTransport>();
  universal_gnss_transport::TcpClientConfig tcp_config;
  tcp_config.host = config.tcp_host;
  tcp_config.port = config.tcp_port;
  tcp_config.connect_timeout_ms = 1000u;
  tcp_config.nonblocking = true;
  tcp_config.read_timeout_ms = 0u;
  const TransportError error = transport->Open(tcp_config);
  if (error != TransportError::kNone)
  {
    events.push_back(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                               universal_gnss::GnssDiagnosticCategory::kTransport,
                               "tcp_open_failed",
                               "Failed to open TCP transport: " + std::string(ToString(error))));
    return nullptr;
  }

  return std::unique_ptr<ByteSource>(transport.release());
#else
  events.push_back(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                             universal_gnss::GnssDiagnosticCategory::kTransport,
                             "tcp_transport_unavailable",
                             "TCP client transport is unavailable on this platform"));
  return nullptr;
#endif
}

}  // namespace

struct ReceiverNode::Impl
{
  static constexpr std::chrono::seconds kParserHealthWindow{3};
  static constexpr double kParserUnhealthyRateHz{1.0};

  explicit Impl(ReceiverNode& owner,
                std::unique_ptr<universal_gnss_transport::ByteSource> injected_source,
                ReceiverNode::DiscoveryFunction discovery_function)
      : owner_(owner)
  {
    startup_events_.reserve(8u);
    const bool using_injected_source = injected_source != nullptr;
    config_ = LoadReceiverNodeConfig(owner_, using_injected_source);

    if (ShouldRunSerialDiscovery(config_, using_injected_source))
    {
      if (!discovery_function)
      {
        discovery_function = MakeDefaultDiscoveryFunction();
      }
      MaybeRunSerialDiscovery(
          owner_, config_, discovery_function, startup_events_, discovery_status_);
    }

    hardware_id_ = BuildHardwareId(config_, injected_source != nullptr);

    session_ = std::make_unique<universal_gnss_driver::ReceiverSession>(config_.session);

    fix_publisher_ = owner_.create_publisher<sensor_msgs::msg::NavSatFix>("fix", 10);
    status_publisher_ = owner_.create_publisher<universal_gnss_ros2::msg::GnssStatus>("status", 10);
    diagnostics_publisher_ =
        owner_.create_publisher<diagnostic_msgs::msg::DiagnosticArray>("diagnostics", 10);
    rtcm_subscription_ = owner_.create_subscription<universal_gnss_ros2::msg::RtcmFrame>(
        "rtcm",
        rclcpp::QoS(rclcpp::KeepLast(50)).reliable(),
        [this](const universal_gnss_ros2::msg::RtcmFrame& message)
        {
          this->OnRtcmMessage(message);
        });

    if (injected_source != nullptr)
    {
      transport_source_ = std::move(injected_source);
      transport_configured_ = true;
      transport_ready_ = true;
      using_injected_source_ = true;
    }
    else
    {
      if (!discovery_status_.attempted || discovery_status_.succeeded)
      {
        transport_source_ = CreateTransportSource(config_, startup_events_);
      }
      transport_configured_ = transport_source_ != nullptr;
      transport_ready_ = transport_source_ != nullptr;
    }

    transport_sink_ = dynamic_cast<universal_gnss_transport::ByteSink*>(transport_source_.get());

    for (const auto& event : startup_events_)
    {
      LogDiagnosticEvent(owner_, event);
    }

    if (transport_source_ != nullptr)
    {
      universal_gnss_driver::ReceiverSessionRunnerConfig runner_config;
      runner_config.read_chunk_size = config_.read_chunk_size;
      runner_config.finalize_session_on_end_of_stream = true;
      runner_config.finalize_session_on_closed = true;
      runner_config.finalize_session_on_error = true;
      runner_.emplace(*transport_source_, *session_, runner_config);
    }

    timer_ = owner_.create_wall_timer(ComputePublishPeriod(config_.publish_rate_hz),
                                      [this]()
                                      {
                                        this->OnTimer();
                                      });
  }

  void OnTimer()
  {
    StepOnce();
    PublishNow();
  }

  void OnRtcmMessage(const universal_gnss_ros2::msg::RtcmFrame& message)
  {
    if (message.data.empty())
    {
      return;
    }

    // Forwarded RTCM updates correction-stream observability only. It must not
    // refresh receiver runtime freshness because corrections can continue while
    // GNSS navigation observations are stale or absent.
    ObserveRtcmSemanticMessage(message);

    if (transport_sink_ == nullptr)
    {
      ++rtcm_forward_write_errors_;
      last_rtcm_forward_failure_message_ = "Receiver transport does not support correction writes";
      return;
    }

    const auto write_all = [&](const std::uint8_t* data,
                               const std::size_t size) -> universal_gnss_transport::WriteResult
    {
      universal_gnss_transport::WriteResult final_result{};
      std::size_t offset = 0u;
      while (offset < size)
      {
        const auto result =
            transport_sink_->Write(data + static_cast<std::ptrdiff_t>(offset), size - offset);
        final_result.bytes_written += result.bytes_written;
        final_result.status = result.status;
        final_result.error = result.error;
        if (result.status != universal_gnss_transport::TransportStatus::kOk ||
            result.bytes_written == 0u)
        {
          break;
        }
        offset += result.bytes_written;
      }
      return final_result;
    };

    const auto result = write_all(message.data.data(), message.data.size());
    if (result.status != universal_gnss_transport::TransportStatus::kOk ||
        result.bytes_written != message.data.size())
    {
      ++rtcm_forward_write_errors_;
      transport_ready_ = transport_source_ != nullptr && transport_source_->IsOpen();
      last_rtcm_forward_failure_message_ =
          "Failed to forward RTCM corrections: " + std::string(ToString(result.error));
      return;
    }

    ++rtcm_forwarded_frames_;
    rtcm_forwarded_bytes_ += result.bytes_written;
    last_rtcm_forward_time_ = SteadyClock::now();
    last_rtcm_forward_message_type_ = message.message_type;
    last_rtcm_forward_failure_message_.reset();
  }

  void ObserveRtcmSemanticMessage(const universal_gnss_ros2::msg::RtcmFrame& message)
  {
    auto timestamp_ns = RtcmTimestampFromRosMessage(message);
    if (!timestamp_ns.has_value())
    {
      timestamp_ns = static_cast<universal_gnss_protocols::ProtocolTimestampNs>(MonotonicNowNs());
    }

    rtcm_forward_framer_.Reset();
    bool observed_frame = false;
    bool parser_failure = false;

    for (const auto byte : message.data)
    {
      const auto parsed = rtcm_forward_framer_.PushByte(byte, timestamp_ns);
      if (parsed.record.has_value())
      {
        rtcm_forward_correction_monitor_.ObserveFrame(*parsed.record);
        observed_frame = true;
        continue;
      }

      if (parsed.status == universal_gnss_protocols::ParserStatus::kInvalidData ||
          parsed.status == universal_gnss_protocols::ParserStatus::kOverflow)
      {
        parser_failure = true;
      }
    }

    const auto finalized = rtcm_forward_framer_.Finalize();
    if (finalized.record.has_value())
    {
      rtcm_forward_correction_monitor_.ObserveFrame(*finalized.record);
      observed_frame = true;
    }
    else if (finalized.status == universal_gnss_protocols::ParserStatus::kTruncated ||
             finalized.status == universal_gnss_protocols::ParserStatus::kInvalidData ||
             finalized.status == universal_gnss_protocols::ParserStatus::kOverflow)
    {
      parser_failure = true;
    }

    rtcm_forward_framer_.Reset();
    if (!observed_frame || parser_failure)
    {
      rtcm_forward_correction_monitor_.ObserveInvalidFrame(timestamp_ns);
    }
  }

  bool StepOnce()
  {
    if (!runner_.has_value())
    {
      return false;
    }

    const std::size_t bytes_before = runner_->metrics().bytes_read;
    const std::size_t runtime_observations_before = session_->metrics().runtime_observations;
    const bool advanced = runner_->StepOnce();
    const auto now = SteadyClock::now();
    const auto& runner_metrics = runner_->metrics();

    if (runner_metrics.bytes_read > bytes_before)
    {
      last_transport_activity_time_ = now;
    }

    if (session_->metrics().runtime_observations > runtime_observations_before)
    {
      last_runtime_observation_time_ = now;
    }

    UpdateReceiverReportedRtcmActivity(now);

    RecordParserCounterDelta(
        session_->metrics().malformed_records, last_malformed_record_count_, recent_malformed_times_, now);
    RecordParserCounterDelta(
        session_->metrics().rejected_records, last_rejected_record_count_, recent_rejected_times_, now);
    RecordParserCounterDelta(
        session_->metrics().parser_anomalies,
        last_parser_anomaly_count_,
        recent_parser_anomaly_times_,
        now);
    PruneParserHistory(now);

    if (runner_metrics.last_status == universal_gnss_transport::TransportStatus::kOk)
    {
      transport_ready_ = transport_source_ != nullptr && transport_source_->IsOpen();
      last_logged_terminal_status_.reset();
      last_logged_transport_error_ = universal_gnss_transport::TransportError::kNone;
    }
    else if (universal_gnss_transport::IsTransportTerminal(runner_metrics.last_status))
    {
      transport_ready_ = false;
      LogTransportTerminalTransition(runner_metrics.last_status, runner_metrics.last_error);
    }

    return advanced;
  }

  universal_gnss::GnssHealthSummary BuildHealthSummary() const
  {
    universal_gnss::GnssHealthSummary summary;
    summary.overall_severity = universal_gnss::GnssDiagnosticSeverity::kOk;

    const auto& state = session_->current_state();
    const auto& session_metrics = session_->metrics();
    const auto now = SteadyClock::now();
    const auto freshness_timeout = RuntimeObservationFreshnessTimeout();
    const std::size_t recent_parser_anomalies = RecentParserEventCount(recent_parser_anomaly_times_, now);
    const double recent_parser_anomaly_rate_hz =
        RecentParserEventRateHz(recent_parser_anomaly_times_, now);
    const bool parser_issue_recent = recent_parser_anomaly_rate_hz >= kParserUnhealthyRateHz;

    const bool receiver_rtcm_active = HasRecentReceiverReportedRtcmCorrections(now);

    summary.fix_valid = state.fix_valid;
    summary.rtk_available = HasRtkAvailability(state);
    summary.correction_available = HasCorrectionAvailability(state) || receiver_rtcm_active;
    summary.receiver_healthy =
        !HasKnownBoolField(state,
                           universal_gnss::GnssCapability::kInterferenceState,
                           state.interference_detected) &&
        !HasKnownBoolField(state,
                           universal_gnss::GnssCapability::kJammingState,
                           state.jamming_detected);
    summary.transport_healthy = transport_ready_;
    summary.parser_healthy = !parser_issue_recent;

    for (const auto& event : startup_events_)
    {
      summary.AddEvent(event);
    }

    if (!transport_configured_ && !using_injected_source_)
    {
      summary.transport_healthy = false;
    }

    if (runner_.has_value())
    {
      const auto& runner_metrics = runner_->metrics();
      if (runner_metrics.read_errors > 0u)
      {
        summary.transport_healthy = false;
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                                   universal_gnss::GnssDiagnosticCategory::kTransport,
                                   "transport_read_error",
                                   "Receiver transport reported read errors"));
      }
      if (runner_metrics.last_status == universal_gnss_transport::TransportStatus::kEndOfStream ||
          runner_metrics.eof_seen)
      {
        summary.transport_healthy = false;
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                   universal_gnss::GnssDiagnosticCategory::kTransport,
                                   "transport_eof",
                                   "Receiver transport reached end of stream"));
      }
      if (runner_metrics.last_status == universal_gnss_transport::TransportStatus::kClosed)
      {
        summary.transport_healthy = false;
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                   universal_gnss::GnssDiagnosticCategory::kTransport,
                                   "transport_closed",
                                   "Receiver transport is closed"));
      }
    }

    if (transport_source_ != nullptr && transport_source_->IsOpen())
    {
      if (!last_transport_activity_time_.has_value())
      {
        if (now - startup_time_ >= freshness_timeout)
        {
          summary.transport_healthy = false;
          summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                     universal_gnss::GnssDiagnosticCategory::kTiming,
                                     "no_data_received",
                                     "No GNSS data has been received yet"));
        }
      }
      else if (now - *last_transport_activity_time_ >= freshness_timeout)
      {
        summary.transport_healthy = false;
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                   universal_gnss::GnssDiagnosticCategory::kTiming,
                                   "transport_data_stale",
                                   "GNSS transport has not produced data recently"));
      }
    }

    if (last_runtime_observation_time_.has_value() &&
        now - *last_runtime_observation_time_ >= freshness_timeout)
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                 universal_gnss::GnssDiagnosticCategory::kRuntime,
                                 "runtime_state_stale",
                                 "GNSS runtime observations have not been received recently"));
    }

    if (parser_issue_recent)
    {
      std::ostringstream stream;
      stream << "Malformed or rejected receiver records exceeded the recent parser anomaly threshold"
             << " (recent_count=" << recent_parser_anomalies
             << ", rate_hz=" << recent_parser_anomaly_rate_hz << ")";
      summary.AddEvent(
          MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                    universal_gnss::GnssDiagnosticCategory::kParser,
                    "malformed_records",
                    stream.str()));
    }

    if (HasKnownBoolField(state,
                          universal_gnss::GnssCapability::kInterferenceState,
                          state.interference_detected))
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                 universal_gnss::GnssDiagnosticCategory::kReceiver,
                                 "interference_detected",
                                 "Receiver reported RF interference"));
    }

    if (HasKnownBoolField(state,
                          universal_gnss::GnssCapability::kJammingState,
                          state.jamming_detected))
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                                 universal_gnss::GnssDiagnosticCategory::kReceiver,
                                 "jamming_detected",
                                 "Receiver reported GNSS jamming"));
    }

    if (transport_sink_ == nullptr)
    {
      summary.AddEvent(
          MakeEvent(universal_gnss::GnssDiagnosticSeverity::kInfo,
                    universal_gnss::GnssDiagnosticCategory::kTransport,
                    "rtcm_forwarding_unavailable",
                    "Receiver transport is read-only; RTCM forwarding is unavailable"));
    }
    else if (HasRecentRtcmForwarding(now))
    {
      summary.AddEvent(
          MakeEvent(universal_gnss::GnssDiagnosticSeverity::kOk,
                    universal_gnss::GnssDiagnosticCategory::kCorrection,
                    "rtcm_forwarding_active",
                    "RTCM corrections are being forwarded to the live receiver transport"));
    }
    else if (last_rtcm_forward_time_.has_value())
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                 universal_gnss::GnssDiagnosticCategory::kCorrection,
                                 "rtcm_forwarding_stale",
                                 "RTCM forwarding has not written a frame recently"));
    }

    if (rtcm_forward_write_errors_ > 0u)
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                 universal_gnss::GnssDiagnosticCategory::kCorrection,
                                 "rtcm_forwarding_error",
                                 last_rtcm_forward_failure_message_.value_or(
                                     "RTCM forwarding reported write errors")));
    }

    if (const auto* ublox_metrics = ActiveUbloxMetrics(); ublox_metrics != nullptr)
    {
      if (HasRecentReceiverReportedRtcmCorrections(now))
      {
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kOk,
                                   universal_gnss::GnssDiagnosticCategory::kCorrection,
                                   "receiver_rtcm_active",
                                   "Receiver reported accepted RTCM corrections"));
      }
      else if (ublox_metrics->receiver_rtcm_messages_used > 0u)
      {
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                   universal_gnss::GnssDiagnosticCategory::kCorrection,
                                   "receiver_rtcm_stale",
                                   "Receiver-reported accepted RTCM corrections are stale"));
      }

      if (ublox_metrics->receiver_rtcm_messages_not_used > 0u)
      {
        summary.AddEvent(
            MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                      universal_gnss::GnssDiagnosticCategory::kCorrection,
                      "receiver_rtcm_not_used",
                      "Receiver reported RTCM messages that were received but not used"));
      }

      if (ublox_metrics->receiver_rtcm_crc_failed > 0u)
      {
        summary.AddEvent(
            MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                      universal_gnss::GnssDiagnosticCategory::kCorrection,
                      "receiver_rtcm_crc_failed",
                      "Receiver reported RTCM messages that failed receiver-side CRC validation"));
      }
    }

    if (const auto* unicore_metrics = ActiveUnicoreMetrics(); unicore_metrics != nullptr)
    {
      if (HasRecentReceiverReportedRtcmCorrections(now))
      {
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kOk,
                                   universal_gnss::GnssDiagnosticCategory::kCorrection,
                                   "receiver_rtcm_active",
                                   "Receiver reported RTCM correction status"));
      }
      else if (unicore_metrics->receiver_rtcm_status_messages_seen > 0u)
      {
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                   universal_gnss::GnssDiagnosticCategory::kCorrection,
                                   "receiver_rtcm_stale",
                                   "Receiver-reported RTCM correction status is stale"));
      }
    }

    return summary;
  }

  void AppendRtcmForwardingStatus(diagnostic_msgs::msg::DiagnosticArray& diagnostics,
                                  const universal_gnss::GnssRuntimeState& state) const
  {
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = "universal_gnss/rtcm_forwarding";
    status.hardware_id = hardware_id_;

    if (transport_sink_ == nullptr)
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
      status.message = "RTCM forwarding unavailable";
    }
    else if (rtcm_forward_write_errors_ > 0u)
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
      status.message = "RTCM forwarding write errors observed";
    }
    else if (HasRecentRtcmForwarding(SteadyClock::now()))
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
      status.message = "RTCM forwarding active";
    }
    else if (last_rtcm_forward_time_.has_value())
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
      status.message = "RTCM forwarding stale";
    }
    else
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
      status.message = "RTCM forwarding idle";
    }

    status.values.push_back(
        MakeKeyValue("forwarding_supported", transport_sink_ != nullptr ? "true" : "false"));
    status.values.push_back(
        MakeKeyValue("forwarded_frame_count", std::to_string(rtcm_forwarded_frames_)));
    status.values.push_back(MakeKeyValue("forwarded_bytes", std::to_string(rtcm_forwarded_bytes_)));
    status.values.push_back(
        MakeKeyValue("write_error_count", std::to_string(rtcm_forward_write_errors_)));
    status.values.push_back(
        MakeKeyValue("receiver_correction_available",
                     (HasCorrectionAvailability(state) ||
                      HasRecentReceiverReportedRtcmCorrections(SteadyClock::now()))
                         ? "true"
                         : "false"));
    if (const auto* ublox_metrics = ActiveUbloxMetrics(); ublox_metrics != nullptr)
    {
      status.values.push_back(
          MakeKeyValue("receiver_rtcm_messages_seen",
                       std::to_string(ublox_metrics->receiver_rtcm_messages_seen)));
      status.values.push_back(
          MakeKeyValue("receiver_rtcm_messages_used",
                       std::to_string(ublox_metrics->receiver_rtcm_messages_used)));
      status.values.push_back(
          MakeKeyValue("receiver_rtcm_messages_not_used",
                       std::to_string(ublox_metrics->receiver_rtcm_messages_not_used)));
      status.values.push_back(
          MakeKeyValue("receiver_rtcm_crc_failed",
                       std::to_string(ublox_metrics->receiver_rtcm_crc_failed)));
      if (ublox_metrics->last_receiver_rtcm_message_type.has_value())
      {
        status.values.push_back(
            MakeKeyValue("receiver_last_message_type",
                         std::to_string(*ublox_metrics->last_receiver_rtcm_message_type)));
      }
    }
    if (const auto* unicore_metrics = ActiveUnicoreMetrics(); unicore_metrics != nullptr)
    {
      status.values.push_back(
          MakeKeyValue("receiver_rtcm_status_messages_seen",
                       std::to_string(unicore_metrics->receiver_rtcm_status_messages_seen)));
      status.values.push_back(
          MakeKeyValue("receiver_rtcm_status_message_count",
                       std::to_string(unicore_metrics->receiver_rtcm_status_message_count)));
      if (unicore_metrics->receiver_last_rtcm_message_type.has_value())
      {
        status.values.push_back(
            MakeKeyValue("receiver_last_message_type",
                         std::to_string(*unicore_metrics->receiver_last_rtcm_message_type)));
      }
      if (unicore_metrics->receiver_last_rtcm_base_station_id.has_value())
      {
        status.values.push_back(
            MakeKeyValue("receiver_last_base_station_id",
                         std::to_string(*unicore_metrics->receiver_last_rtcm_base_station_id)));
      }
      if (unicore_metrics->receiver_last_rtcm_satellites_in_message.has_value())
      {
        status.values.push_back(MakeKeyValue(
            "receiver_last_satellites_in_message",
            std::to_string(*unicore_metrics->receiver_last_rtcm_satellites_in_message)));
      }
    }
    if (last_rtcm_forward_message_type_.has_value())
    {
      status.values.push_back(
          MakeKeyValue("last_message_type", std::to_string(*last_rtcm_forward_message_type_)));
    }
    if (last_rtcm_forward_time_.has_value())
    {
      const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          SteadyClock::now() - *last_rtcm_forward_time_);
      std::ostringstream stream;
      stream << (static_cast<double>(age_ms.count()) / 1000.0);
      status.values.push_back(MakeKeyValue("last_frame_age_s", stream.str()));
    }
    if (last_rtcm_forward_failure_message_.has_value())
    {
      status.values.push_back(MakeKeyValue("last_failure", *last_rtcm_forward_failure_message_));
    }

    diagnostics.status.push_back(std::move(status));
  }

  void AppendDiscoveryStatus(diagnostic_msgs::msg::DiagnosticArray& diagnostics) const
  {
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = "universal_gnss/discovery";
    status.hardware_id = hardware_id_;

    if (!discovery_status_.attempted)
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
      status.message = "Discovery not used";
    }
    else if (discovery_status_.succeeded)
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
      status.message = "Receiver discovery succeeded";
    }
    else
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
      status.message = "Receiver discovery failed";
    }

    status.values.push_back(
        MakeKeyValue("attempted", discovery_status_.attempted ? "true" : "false"));
    status.values.push_back(
        MakeKeyValue("succeeded", discovery_status_.succeeded ? "true" : "false"));
    status.values.push_back(
        MakeKeyValue("include_platform_uarts",
                     config_.discovery_include_platform_uarts ? "true" : "false"));
    status.values.push_back(MakeKeyValue("allow_generic_nmea",
                                         config_.discovery_allow_generic_nmea ? "true" : "false"));
    status.values.push_back(
        MakeKeyValue("timeout_ms", std::to_string(config_.discovery_timeout_ms)));
    status.values.push_back(
        MakeKeyValue("max_probe_bytes", std::to_string(config_.discovery_max_probe_bytes)));

    if (discovery_status_.result.has_value())
    {
      const auto& result = *discovery_status_.result;
      status.values.push_back(MakeKeyValue("path", result.path));
      status.values.push_back(MakeKeyValue("detected_device", result.path));
      if (result.selected_baud.has_value())
      {
        status.values.push_back(MakeKeyValue("baud", std::to_string(*result.selected_baud)));
        status.values.push_back(
            MakeKeyValue("detected_baud", std::to_string(*result.selected_baud)));
      }
      status.values.push_back(
          MakeKeyValue("family", universal_gnss_driver::ToString(result.detected_family)));
      status.values.push_back(
          MakeKeyValue("detected_family", universal_gnss_driver::ToString(result.detected_family)));
      status.values.push_back(
          MakeKeyValue("confidence", universal_gnss_driver::ToString(result.confidence)));
      status.values.push_back(
          MakeKeyValue("discovery_confidence", std::to_string(result.discovery_score)));
      status.values.push_back(MakeKeyValue("discovery_reason", result.reason));
      status.values.push_back(
          MakeKeyValue("evidence", BuildDiscoveryEvidenceSummary(result.evidence)));
      if (result.stable_id.has_value())
      {
        status.values.push_back(MakeKeyValue("stable_id", *result.stable_id));
      }
    }

    if (discovery_status_.failure_reason.has_value())
    {
      status.values.push_back(MakeKeyValue("failure_reason", *discovery_status_.failure_reason));
    }

    diagnostics.status.push_back(std::move(status));
  }

  void AppendParserStatus(diagnostic_msgs::msg::DiagnosticArray& diagnostics) const
  {
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = "universal_gnss/parser_counters";
    status.hardware_id = hardware_id_;

    const auto& session_metrics = session_->metrics();
    const auto now = SteadyClock::now();
    const std::size_t recent_malformed_records =
        RecentParserEventCount(recent_malformed_times_, now);
    const std::size_t recent_rejected_records =
        RecentParserEventCount(recent_rejected_times_, now);
    const std::size_t recent_parser_anomalies =
        RecentParserEventCount(recent_parser_anomaly_times_, now);
    const double recent_parser_anomaly_rate_hz =
        RecentParserEventRateHz(recent_parser_anomaly_times_, now);

    if (recent_parser_anomaly_rate_hz >= kParserUnhealthyRateHz)
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
      status.message = "Recent parser anomaly rate is elevated";
    }
    else
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
      status.message = "Parser healthy";
    }

    status.values.push_back(
        MakeKeyValue("selected_session",
                     session_metrics.selected_session_kind.has_value()
                         ? universal_gnss_driver::ToString(*session_metrics.selected_session_kind)
                         : "undecided"));
    status.values.push_back(
        MakeKeyValue("parser_health_window_s",
                     std::to_string(kParserHealthWindow.count())));
    status.values.push_back(
        MakeKeyValue("parser_unhealthy_rate_threshold_hz",
                     std::to_string(kParserUnhealthyRateHz)));
    status.values.push_back(
        MakeKeyValue("recent_parser_anomalies", std::to_string(recent_parser_anomalies)));
    status.values.push_back(
        MakeKeyValue("recent_parser_anomaly_rate_hz",
                     FormatFloatingPointValue(recent_parser_anomaly_rate_hz)));
    status.values.push_back(
        MakeKeyValue("recent_malformed_records", std::to_string(recent_malformed_records)));
    status.values.push_back(
        MakeKeyValue("recent_rejected_records", std::to_string(recent_rejected_records)));
    status.values.push_back(
        MakeKeyValue("malformed_records_total", std::to_string(session_metrics.malformed_records)));
    status.values.push_back(
        MakeKeyValue("rejected_records_total", std::to_string(session_metrics.rejected_records)));
    status.values.push_back(
        MakeKeyValue("parser_anomalies_total", std::to_string(session_metrics.parser_anomalies)));
    status.values.push_back(
        MakeKeyValue("unknown_records_total", std::to_string(session_metrics.unknown_records)));
    status.values.push_back(
        MakeKeyValue("runtime_observations", std::to_string(session_metrics.runtime_observations)));
    status.values.push_back(
        MakeKeyValue("runtime_updates", std::to_string(session_metrics.runtime_updates)));

    if (const auto* unicore_metrics = ActiveUnicoreMetrics(); unicore_metrics != nullptr)
    {
      status.values.push_back(
          MakeKeyValue("unicore_lines_seen", std::to_string(unicore_metrics->lines_seen)));
      status.values.push_back(MakeKeyValue("unicore_ascii_records_seen",
                                           std::to_string(unicore_metrics->ascii_records_seen)));
      status.values.push_back(MakeKeyValue("unicore_binary_frames_seen",
                                           std::to_string(unicore_metrics->binary_frames_seen)));
      status.values.push_back(
          MakeKeyValue("unicore_records_parsed", std::to_string(unicore_metrics->records_parsed)));
      status.values.push_back(MakeKeyValue("unicore_records_rejected",
                                           std::to_string(unicore_metrics->records_rejected)));
      status.values.push_back(MakeKeyValue("unicore_malformed_lines",
                                           std::to_string(unicore_metrics->malformed_lines)));
      status.values.push_back(MakeKeyValue("unicore_malformed_frames",
                                           std::to_string(unicore_metrics->malformed_frames)));
      status.values.push_back(MakeKeyValue("unicore_unknown_records",
                                           std::to_string(unicore_metrics->unknown_records)));
    }

    diagnostics.status.push_back(std::move(status));
  }

  void PublishNow()
  {
    auto state = session_->current_state();
    if (!state.timestamp_ns.has_value())
    {
      state.timestamp_ns = owner_.now().nanoseconds();
    }

    last_status_message_ = ToGnssStatusMessage(state);

    auto summary = BuildHealthSummary();
    last_diagnostics_message_ = ToDiagnosticArrayMessage(summary, "universal_gnss", hardware_id_);
    AppendResolvedDiagnosticEvents(*last_diagnostics_message_, summary, state.timestamp_ns);
    last_active_events_ = summary.events;
    if (last_diagnostics_message_->header.stamp.sec == 0 &&
        last_diagnostics_message_->header.stamp.nanosec == 0u)
    {
      last_diagnostics_message_->header.stamp = ToRosTime(state.timestamp_ns);
    }
    last_diagnostics_message_->header.frame_id = config_.frame_id;
    AppendDiscoveryStatus(*last_diagnostics_message_);
    AppendRtcmForwardingStatus(*last_diagnostics_message_, state);
    AppendRtcmSemanticObservationStatuses(
        *last_diagnostics_message_,
        universal_gnss_protocols::BuildRtcmSemanticObservations(
            rtcm_forward_correction_monitor_,
            static_cast<universal_gnss_protocols::ProtocolTimestampNs>(MonotonicNowNs())),
        "universal_gnss",
        hardware_id_);
    AppendParserStatus(*last_diagnostics_message_);

    if (CanPublishFixMessage(state))
    {
      last_fix_message_ = ToNavSatFixMessage(state);
      last_fix_message_->header.frame_id = config_.frame_id;
      fix_publisher_->publish(*last_fix_message_);
    }
    else
    {
      last_fix_message_.reset();
    }

    status_publisher_->publish(*last_status_message_);
    diagnostics_publisher_->publish(*last_diagnostics_message_);
  }

  void AppendResolvedDiagnosticEvents(
      diagnostic_msgs::msg::DiagnosticArray& diagnostics,
      const universal_gnss::GnssHealthSummary& summary,
      const std::optional<universal_gnss::GnssTimestampNs> timestamp_ns) const
  {
    for (const auto& previous_event : last_active_events_)
    {
      const bool still_active = std::any_of(
          summary.events.begin(),
          summary.events.end(),
          [&](const universal_gnss::GnssDiagnosticEvent& current_event)
          {
            return current_event.code == previous_event.code;
          });
      if (still_active)
      {
        continue;
      }

      auto cleared_event = previous_event;
      cleared_event.severity = universal_gnss::GnssDiagnosticSeverity::kOk;
      cleared_event.message = "Diagnostic condition cleared";
      cleared_event.timestamp_ns = timestamp_ns;
      diagnostics.status.push_back(
          ToDiagnosticStatusMessage(cleared_event, "universal_gnss", hardware_id_));
    }
  }

  bool publishers_ready() const
  {
    return fix_publisher_ != nullptr && status_publisher_ != nullptr &&
           diagnostics_publisher_ != nullptr && rtcm_subscription_ != nullptr;
  }

  bool HasFreshRuntimeState() const
  {
    return last_runtime_observation_time_.has_value() &&
           (SteadyClock::now() - *last_runtime_observation_time_ <
            RuntimeObservationFreshnessTimeout());
  }

  SteadyClock::duration RuntimeObservationFreshnessTimeout() const
  {
    double timeout_seconds = config_.runtime_observation_fallback_timeout_s;
    if (config_.expected_runtime_observation_rate_hz > 0.0)
    {
      timeout_seconds =
          kRuntimeObservationJitterPeriods / config_.expected_runtime_observation_rate_hz;
    }

    return std::chrono::duration_cast<SteadyClock::duration>(
        std::chrono::duration<double>(timeout_seconds));
  }

  SteadyClock::duration RtcmForwardingActivityTimeout() const
  {
    return std::chrono::duration_cast<SteadyClock::duration>(
        std::chrono::duration<double>(config_.rtcm_forwarding_activity_timeout_s));
  }

  bool HasRecentRtcmForwarding(const SteadyClock::time_point now) const
  {
    return last_rtcm_forward_time_.has_value() &&
           now - *last_rtcm_forward_time_ < RtcmForwardingActivityTimeout();
  }

  void UpdateReceiverReportedRtcmActivity(const SteadyClock::time_point now)
  {
    if (const auto* ublox_metrics = ActiveUbloxMetrics(); ublox_metrics != nullptr)
    {
      const auto current_count = ublox_metrics->receiver_rtcm_messages_used;
      if (current_count < last_ublox_receiver_rtcm_used_count_)
      {
        last_ublox_receiver_rtcm_used_time_.reset();
      }
      if (current_count > last_ublox_receiver_rtcm_used_count_)
      {
        last_ublox_receiver_rtcm_used_time_ = now;
      }
      last_ublox_receiver_rtcm_used_count_ = current_count;
    }

    if (const auto* unicore_metrics = ActiveUnicoreMetrics(); unicore_metrics != nullptr)
    {
      const auto current_count = unicore_metrics->receiver_rtcm_status_messages_seen;
      if (current_count < last_unicore_receiver_rtcm_status_count_)
      {
        last_unicore_receiver_rtcm_status_time_.reset();
      }
      if (current_count > last_unicore_receiver_rtcm_status_count_)
      {
        last_unicore_receiver_rtcm_status_time_ = now;
      }
      last_unicore_receiver_rtcm_status_count_ = current_count;
    }
  }

  void RecordParserCounterDelta(const std::size_t current_count,
                                std::size_t& last_count,
                                std::deque<SteadyClock::time_point>& timestamps,
                                const SteadyClock::time_point now)
  {
    if (current_count < last_count)
    {
      last_count = current_count;
      timestamps.clear();
      return;
    }

    for (std::size_t index = last_count; index < current_count; ++index)
    {
      timestamps.push_back(now);
    }

    last_count = current_count;
  }

  void PruneParserHistory(const SteadyClock::time_point now)
  {
    PruneParserHistory(recent_malformed_times_, now);
    PruneParserHistory(recent_rejected_times_, now);
    PruneParserHistory(recent_parser_anomaly_times_, now);
  }

  void PruneParserHistory(std::deque<SteadyClock::time_point>& timestamps,
                          const SteadyClock::time_point now)
  {
    while (!timestamps.empty() && now - timestamps.front() >= kParserHealthWindow)
    {
      timestamps.pop_front();
    }
  }

  std::size_t RecentParserEventCount(const std::deque<SteadyClock::time_point>& timestamps,
                                     const SteadyClock::time_point now) const
  {
    std::size_t count = 0u;
    for (const auto timestamp : timestamps)
    {
      if (now - timestamp < kParserHealthWindow)
      {
        ++count;
      }
    }
    return count;
  }

  double RecentParserEventRateHz(const std::deque<SteadyClock::time_point>& timestamps,
                                 const SteadyClock::time_point now) const
  {
    return static_cast<double>(RecentParserEventCount(timestamps, now)) /
           static_cast<double>(kParserHealthWindow.count());
  }

  std::string FormatFloatingPointValue(const double value) const
  {
    std::ostringstream stream;
    stream << value;
    return stream.str();
  }

  const universal_gnss_driver::UbloxSessionMetrics* ActiveUbloxMetrics() const
  {
    if (session_ == nullptr || session_->metrics().selected_session_kind !=
                                   universal_gnss_driver::ReceiverSessionKind::kUblox)
    {
      return nullptr;
    }

    return &session_->ublox_metrics();
  }

  const universal_gnss_driver::UnicoreSessionMetrics* ActiveUnicoreMetrics() const
  {
    if (session_ == nullptr || session_->metrics().selected_session_kind !=
                                   universal_gnss_driver::ReceiverSessionKind::kUnicore)
    {
      return nullptr;
    }

    return &session_->unicore_metrics();
  }

  bool HasRecentReceiverReportedRtcmCorrections(const SteadyClock::time_point now) const
  {
    if (const auto* ublox_metrics = ActiveUbloxMetrics();
        ublox_metrics != nullptr && ublox_metrics->receiver_rtcm_messages_used > 0u &&
        last_ublox_receiver_rtcm_used_time_.has_value() &&
        now - *last_ublox_receiver_rtcm_used_time_ < RtcmForwardingActivityTimeout())
    {
      return true;
    }

    if (const auto* unicore_metrics = ActiveUnicoreMetrics();
        unicore_metrics != nullptr && unicore_metrics->receiver_rtcm_status_messages_seen > 0u &&
        last_unicore_receiver_rtcm_status_time_.has_value() &&
        now - *last_unicore_receiver_rtcm_status_time_ < RtcmForwardingActivityTimeout())
    {
      return true;
    }

    return false;
  }

  bool CanPublishFixMessage(const universal_gnss::GnssRuntimeState& state) const
  {
    if (!transport_ready_ || !HasFreshRuntimeState() || !state.latitude_deg.has_value() ||
        !state.longitude_deg.has_value())
    {
      return false;
    }

    return std::isfinite(*state.latitude_deg) && std::isfinite(*state.longitude_deg);
  }

  void LogTransportTerminalTransition(const universal_gnss_transport::TransportStatus status,
                                      const universal_gnss_transport::TransportError error)
  {
    if (last_logged_terminal_status_.has_value() && *last_logged_terminal_status_ == status &&
        last_logged_transport_error_ == error)
    {
      return;
    }

    last_logged_terminal_status_ = status;
    last_logged_transport_error_ = error;

    switch (status)
    {
      case universal_gnss_transport::TransportStatus::kEndOfStream:
        RCLCPP_WARN(owner_.get_logger(), "GNSS transport reached end of stream");
        break;
      case universal_gnss_transport::TransportStatus::kClosed:
        RCLCPP_WARN(owner_.get_logger(), "GNSS transport closed");
        break;
      case universal_gnss_transport::TransportStatus::kError:
        RCLCPP_ERROR(owner_.get_logger(), "GNSS transport read error: %s", ToString(error));
        break;
      case universal_gnss_transport::TransportStatus::kOk:
      default:
        break;
    }
  }

  ReceiverNode& owner_;
  ReceiverNodeConfig config_{};
  ReceiverDiscoveryStatus discovery_status_{};
  std::string hardware_id_{};
  std::unique_ptr<universal_gnss_driver::ReceiverSession> session_{};
  std::unique_ptr<universal_gnss_transport::ByteSource> transport_source_{};
  std::optional<universal_gnss_driver::ReceiverSessionRunner> runner_{};
  universal_gnss_transport::ByteSink* transport_sink_{nullptr};
  rclcpp::Publisher<sensor_msgs::msg::NavSatFix>::SharedPtr fix_publisher_{};
  rclcpp::Publisher<universal_gnss_ros2::msg::GnssStatus>::SharedPtr status_publisher_{};
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_publisher_{};
  rclcpp::Subscription<universal_gnss_ros2::msg::RtcmFrame>::SharedPtr rtcm_subscription_{};
  rclcpp::TimerBase::SharedPtr timer_{};
  std::vector<universal_gnss::GnssDiagnosticEvent> startup_events_{};
  std::optional<sensor_msgs::msg::NavSatFix> last_fix_message_{};
  std::optional<universal_gnss_ros2::msg::GnssStatus> last_status_message_{};
  std::optional<diagnostic_msgs::msg::DiagnosticArray> last_diagnostics_message_{};
  SteadyClock::time_point startup_time_{SteadyClock::now()};
  std::optional<SteadyClock::time_point> last_transport_activity_time_{};
  std::optional<SteadyClock::time_point> last_runtime_observation_time_{};
  std::optional<SteadyClock::time_point> last_rtcm_forward_time_{};
  std::optional<SteadyClock::time_point> last_ublox_receiver_rtcm_used_time_{};
  std::optional<SteadyClock::time_point> last_unicore_receiver_rtcm_status_time_{};
  std::optional<std::uint16_t> last_rtcm_forward_message_type_{};
  std::optional<universal_gnss_transport::TransportStatus> last_logged_terminal_status_{};
  std::optional<std::string> last_rtcm_forward_failure_message_{};
  universal_gnss_transport::TransportError last_logged_transport_error_{
      universal_gnss_transport::TransportError::kNone};
  universal_gnss_protocols::RtcmFrameFramer rtcm_forward_framer_{};
  universal_gnss_protocols::RtcmCorrectionMonitor rtcm_forward_correction_monitor_{};
  universal_gnss::GnssDiagnosticEvents last_active_events_{};
  std::size_t rtcm_forwarded_frames_{0u};
  std::size_t rtcm_forwarded_bytes_{0u};
  std::size_t rtcm_forward_write_errors_{0u};
  std::size_t last_ublox_receiver_rtcm_used_count_{0u};
  std::size_t last_unicore_receiver_rtcm_status_count_{0u};
  std::size_t last_malformed_record_count_{0u};
  std::size_t last_rejected_record_count_{0u};
  std::size_t last_parser_anomaly_count_{0u};
  std::deque<SteadyClock::time_point> recent_malformed_times_{};
  std::deque<SteadyClock::time_point> recent_rejected_times_{};
  std::deque<SteadyClock::time_point> recent_parser_anomaly_times_{};
  bool transport_configured_{false};
  bool transport_ready_{false};
  bool using_injected_source_{false};
};

ReceiverNode::ReceiverNode(const rclcpp::NodeOptions& options)
    : rclcpp::Node("universal_gnss_receiver", options),
      impl_(std::make_unique<Impl>(*this,
                                   std::unique_ptr<universal_gnss_transport::ByteSource>{},
                                   MakeDefaultDiscoveryFunction()))
{
}

ReceiverNode::ReceiverNode(DiscoveryFunction discovery_function, const rclcpp::NodeOptions& options)
    : rclcpp::Node("universal_gnss_receiver", options),
      impl_(std::make_unique<Impl>(*this,
                                   std::unique_ptr<universal_gnss_transport::ByteSource>{},
                                   std::move(discovery_function)))
{
}

ReceiverNode::ReceiverNode(std::unique_ptr<universal_gnss_transport::ByteSource> source,
                           const rclcpp::NodeOptions& options)
    : rclcpp::Node("universal_gnss_receiver", options),
      impl_(std::make_unique<Impl>(*this, std::move(source), MakeDefaultDiscoveryFunction()))
{
}

ReceiverNode::ReceiverNode(std::unique_ptr<universal_gnss_transport::ByteSource> source,
                           DiscoveryFunction discovery_function,
                           const rclcpp::NodeOptions& options)
    : rclcpp::Node("universal_gnss_receiver", options),
      impl_(std::make_unique<Impl>(*this, std::move(source), std::move(discovery_function)))
{
}

ReceiverNode::~ReceiverNode() = default;

bool ReceiverNode::StepOnce()
{
  return impl_->StepOnce();
}

void ReceiverNode::PublishNow()
{
  impl_->PublishNow();
}

bool ReceiverNode::has_transport_source() const
{
  return impl_->transport_source_ != nullptr;
}

bool ReceiverNode::publishers_ready() const
{
  return impl_->publishers_ready();
}

const universal_gnss::GnssRuntimeState& ReceiverNode::current_state() const
{
  return impl_->session_->current_state();
}

const std::optional<sensor_msgs::msg::NavSatFix>& ReceiverNode::last_fix_message() const
{
  return impl_->last_fix_message_;
}

const std::optional<universal_gnss_ros2::msg::GnssStatus>& ReceiverNode::last_status_message() const
{
  return impl_->last_status_message_;
}

const std::optional<diagnostic_msgs::msg::DiagnosticArray>& ReceiverNode::last_diagnostics_message()
    const
{
  return impl_->last_diagnostics_message_;
}

}  // namespace universal_gnss_ros2
