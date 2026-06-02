#include "universal_gnss_ros2/receiver_node.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cmath>
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
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_driver/receiver_session_runner.hpp"
#include "universal_gnss_ros2/diagnostic_adapter.hpp"
#include "universal_gnss_ros2/gnss_status_adapter.hpp"
#include "universal_gnss_ros2/navsat_fix_adapter.hpp"
#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/tcp_client_transport.hpp"
#include "universal_gnss_transport/posix_serial_transport.hpp"

namespace universal_gnss_ros2
{

namespace
{

enum class ReceiverTransportKind : std::uint8_t
{
  kSerial = 0,
  kTcp = 1,
};

struct ReceiverNodeConfig
{
  universal_gnss_driver::ReceiverSessionConfig session{};
  ReceiverTransportKind transport_kind{ReceiverTransportKind::kSerial};
  std::string receiver_family_name{"auto"};
  std::string transport_name{"serial"};
  std::string serial_device{};
  std::uint32_t serial_baud{115200u};
  std::string tcp_host{};
  std::uint16_t tcp_port{0u};
  double publish_rate_hz{10.0};
  std::string frame_id{"gnss"};
};

using SteadyClock = std::chrono::steady_clock;

std::string ToLowerCopy(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
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

bool HasRtkAvailability(const universal_gnss::GnssRuntimeState& state)
{
  if (state.fix_type == universal_gnss::GnssFixType::kRtkFloat ||
      state.fix_type == universal_gnss::GnssFixType::kRtkFixed)
  {
    return true;
  }

  return universal_gnss::HasValueAvailable(state, universal_gnss::GnssCapability::kRtkMode) &&
         state.rtk_mode.has_value() &&
         *state.rtk_mode != universal_gnss::GnssRtkMode::kNone &&
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

ReceiverNodeConfig LoadReceiverNodeConfig(rclcpp::Node& node, const bool using_injected_source)
{
  ReceiverNodeConfig config;

  config.receiver_family_name =
      ToLowerCopy(node.declare_parameter<std::string>("receiver_family", "auto"));
  config.transport_name = ToLowerCopy(node.declare_parameter<std::string>("transport", "serial"));
  config.serial_device = node.declare_parameter<std::string>("serial_device", "");
  const auto serial_baud = node.declare_parameter<std::int64_t>("serial_baud", 115200);
  config.tcp_host = node.declare_parameter<std::string>("tcp_host", "");
  const auto tcp_port = node.declare_parameter<std::int64_t>("tcp_port", 0);
  config.publish_rate_hz = node.declare_parameter<double>("publish_rate_hz", 10.0);
  config.frame_id = node.declare_parameter<std::string>("frame_id", "gnss");

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
  else
  {
    ThrowInvalidParameter(
        node, "receiver_family", "expected one of: auto, nmea, ublox, unicore");
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

  if (serial_baud <= 0 || serial_baud > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
  {
    ThrowInvalidParameter(node, "serial_baud", "must be in the 1..4294967295 range");
  }
  config.serial_baud = static_cast<std::uint32_t>(serial_baud);

  if (!std::isfinite(config.publish_rate_hz) || !(config.publish_rate_hz > 0.0))
  {
    ThrowInvalidParameter(node, "publish_rate_hz", "must be finite and strictly positive");
  }

  if (config.frame_id.empty())
  {
    ThrowInvalidParameter(node, "frame_id", "must not be empty");
  }

  if (!using_injected_source && config.transport_kind == ReceiverTransportKind::kSerial &&
      config.serial_device.empty())
  {
    ThrowInvalidParameter(node, "serial_device", "must be set when transport=serial");
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
    const ReceiverNodeConfig& config,
    std::vector<universal_gnss::GnssDiagnosticEvent>& events)
{
  using universal_gnss_transport::ByteSource;
  using universal_gnss_transport::TransportError;

  if (config.transport_kind == ReceiverTransportKind::kSerial)
  {
#if defined(UNIVERSAL_GNSS_TRANSPORT_HAS_POSIX_SERIAL)
    if (config.serial_device.empty())
    {
      events.push_back(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                 universal_gnss::GnssDiagnosticCategory::kConfiguration,
                                 "serial_device_missing",
                                 "transport=serial requires serial_device"));
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
      events.push_back(MakeEvent(
          universal_gnss::GnssDiagnosticSeverity::kError,
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
  static constexpr std::chrono::seconds kStaleTimeout{3};

  explicit Impl(ReceiverNode& owner,
                std::unique_ptr<universal_gnss_transport::ByteSource> injected_source)
      : owner_(owner)
  {
    startup_events_.reserve(8u);
    const bool using_injected_source = injected_source != nullptr;
    config_ = LoadReceiverNodeConfig(owner_, using_injected_source);
    hardware_id_ = BuildHardwareId(config_, injected_source != nullptr);

    session_ = std::make_unique<universal_gnss_driver::ReceiverSession>(config_.session);

    fix_publisher_ = owner_.create_publisher<sensor_msgs::msg::NavSatFix>("fix", 10);
    status_publisher_ =
        owner_.create_publisher<universal_gnss_ros2::msg::GnssStatus>("status", 10);
    diagnostics_publisher_ =
        owner_.create_publisher<diagnostic_msgs::msg::DiagnosticArray>("diagnostics", 10);

    if (injected_source != nullptr)
    {
      transport_source_ = std::move(injected_source);
      transport_configured_ = true;
      transport_ready_ = true;
      using_injected_source_ = true;
    }
    else
    {
      transport_source_ = CreateTransportSource(config_, startup_events_);
      transport_configured_ = transport_source_ != nullptr;
      transport_ready_ = transport_source_ != nullptr;
    }

    for (const auto& event : startup_events_)
    {
      LogDiagnosticEvent(owner_, event);
    }

    if (transport_source_ != nullptr)
    {
      universal_gnss_driver::ReceiverSessionRunnerConfig runner_config;
      runner_config.read_chunk_size = 512u;
      runner_config.finalize_session_on_end_of_stream = true;
      runner_config.finalize_session_on_closed = true;
      runner_config.finalize_session_on_error = true;
      runner_.emplace(*transport_source_, *session_, runner_config);
    }

    timer_ = owner_.create_wall_timer(ComputePublishPeriod(config_.publish_rate_hz), [this]() {
      this->OnTimer();
    });
  }

  void OnTimer()
  {
    StepOnce();
    PublishNow();
  }

  bool StepOnce()
  {
    if (!runner_.has_value())
    {
      return false;
    }

    const std::size_t bytes_before = runner_->metrics().bytes_read;
    const std::size_t runtime_updates_before = session_->metrics().runtime_updates;
    const bool advanced = runner_->StepOnce();
    const auto now = SteadyClock::now();
    const auto& runner_metrics = runner_->metrics();

    if (runner_metrics.bytes_read > bytes_before)
    {
      last_transport_activity_time_ = now;
    }

    if (session_->metrics().runtime_updates > runtime_updates_before)
    {
      last_runtime_update_time_ = now;
    }

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

    summary.fix_valid = state.fix_valid;
    summary.rtk_available = HasRtkAvailability(state);
    summary.correction_available = HasCorrectionAvailability(state);
    summary.receiver_healthy =
        !HasKnownBoolField(state,
                          universal_gnss::GnssCapability::kInterferenceState,
                          state.interference_detected) &&
        !HasKnownBoolField(
            state, universal_gnss::GnssCapability::kJammingState, state.jamming_detected);
    summary.transport_healthy = transport_ready_;
    summary.parser_healthy = session_metrics.malformed_records == 0u;

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
        summary.AddEvent(MakeEvent(
            universal_gnss::GnssDiagnosticSeverity::kError,
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

    const auto now = SteadyClock::now();
    if (transport_source_ != nullptr && transport_source_->IsOpen())
    {
      if (!last_transport_activity_time_.has_value())
      {
        if (now - startup_time_ >= kStaleTimeout)
        {
          summary.transport_healthy = false;
          summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                     universal_gnss::GnssDiagnosticCategory::kTiming,
                                     "no_data_received",
                                     "No GNSS data has been received yet"));
        }
      }
      else if (now - *last_transport_activity_time_ >= kStaleTimeout)
      {
        summary.transport_healthy = false;
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                   universal_gnss::GnssDiagnosticCategory::kTiming,
                                   "transport_data_stale",
                                   "GNSS transport has not produced data recently"));
      }
    }

    if (last_runtime_update_time_.has_value() && now - *last_runtime_update_time_ >= kStaleTimeout)
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                 universal_gnss::GnssDiagnosticCategory::kRuntime,
                                 "runtime_state_stale",
                                 "GNSS runtime state has not been updated recently"));
    }

    if (session_metrics.malformed_records > 0u)
    {
      summary.AddEvent(MakeEvent(
          universal_gnss::GnssDiagnosticSeverity::kWarning,
          universal_gnss::GnssDiagnosticCategory::kParser,
          "malformed_records",
          "Malformed records were observed while parsing receiver data"));
    }

    if (HasKnownBoolField(
            state, universal_gnss::GnssCapability::kInterferenceState, state.interference_detected))
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                 universal_gnss::GnssDiagnosticCategory::kReceiver,
                                 "interference_detected",
                                 "Receiver reported RF interference"));
    }

    if (HasKnownBoolField(
            state, universal_gnss::GnssCapability::kJammingState, state.jamming_detected))
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                                 universal_gnss::GnssDiagnosticCategory::kReceiver,
                                 "jamming_detected",
                                 "Receiver reported GNSS jamming"));
    }

    return summary;
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
    last_diagnostics_message_ =
        ToDiagnosticArrayMessage(summary, "universal_gnss", hardware_id_);
    if (last_diagnostics_message_->header.stamp.sec == 0 &&
        last_diagnostics_message_->header.stamp.nanosec == 0u)
    {
      last_diagnostics_message_->header.stamp = ToRosTime(state.timestamp_ns);
    }
    last_diagnostics_message_->header.frame_id = config_.frame_id;

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

  bool publishers_ready() const
  {
    return fix_publisher_ != nullptr && status_publisher_ != nullptr &&
           diagnostics_publisher_ != nullptr;
  }

  bool HasFreshRuntimeState() const
  {
    return last_runtime_update_time_.has_value() &&
           (SteadyClock::now() - *last_runtime_update_time_ < kStaleTimeout);
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
        RCLCPP_ERROR(
            owner_.get_logger(), "GNSS transport read error: %s", ToString(error));
        break;
      case universal_gnss_transport::TransportStatus::kOk:
      default:
        break;
    }
  }

  ReceiverNode& owner_;
  ReceiverNodeConfig config_{};
  std::string hardware_id_{};
  std::unique_ptr<universal_gnss_driver::ReceiverSession> session_{};
  std::unique_ptr<universal_gnss_transport::ByteSource> transport_source_{};
  std::optional<universal_gnss_driver::ReceiverSessionRunner> runner_{};
  rclcpp::Publisher<sensor_msgs::msg::NavSatFix>::SharedPtr fix_publisher_{};
  rclcpp::Publisher<universal_gnss_ros2::msg::GnssStatus>::SharedPtr status_publisher_{};
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_publisher_{};
  rclcpp::TimerBase::SharedPtr timer_{};
  std::vector<universal_gnss::GnssDiagnosticEvent> startup_events_{};
  std::optional<sensor_msgs::msg::NavSatFix> last_fix_message_{};
  std::optional<universal_gnss_ros2::msg::GnssStatus> last_status_message_{};
  std::optional<diagnostic_msgs::msg::DiagnosticArray> last_diagnostics_message_{};
  SteadyClock::time_point startup_time_{SteadyClock::now()};
  std::optional<SteadyClock::time_point> last_transport_activity_time_{};
  std::optional<SteadyClock::time_point> last_runtime_update_time_{};
  std::optional<universal_gnss_transport::TransportStatus> last_logged_terminal_status_{};
  universal_gnss_transport::TransportError last_logged_transport_error_{
      universal_gnss_transport::TransportError::kNone};
  bool transport_configured_{false};
  bool transport_ready_{false};
  bool using_injected_source_{false};
};

ReceiverNode::ReceiverNode(const rclcpp::NodeOptions& options)
    : rclcpp::Node("universal_gnss_receiver", options),
      impl_(std::make_unique<Impl>(*this, std::unique_ptr<universal_gnss_transport::ByteSource>{}))
{
}

ReceiverNode::ReceiverNode(std::unique_ptr<universal_gnss_transport::ByteSource> source,
                           const rclcpp::NodeOptions& options)
    : rclcpp::Node("universal_gnss_receiver", options),
      impl_(std::make_unique<Impl>(*this, std::move(source)))
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

const std::optional<universal_gnss_ros2::msg::GnssStatus>& ReceiverNode::last_status_message()
    const
{
  return impl_->last_status_message_;
}

const std::optional<diagnostic_msgs::msg::DiagnosticArray>&
ReceiverNode::last_diagnostics_message() const
{
  return impl_->last_diagnostics_message_;
}

}  // namespace universal_gnss_ros2
