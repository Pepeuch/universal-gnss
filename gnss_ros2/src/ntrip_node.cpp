#include "universal_gnss_ros2/ntrip_node.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"
#include "rclcpp/time.hpp"
#include "rtcm_diagnostic_projection.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_ntrip/ntrip_client.hpp"
#include "universal_gnss_protocols/rtcm_correction_monitor.hpp"
#include "universal_gnss_ros2/diagnostic_adapter.hpp"
#include "universal_gnss_ros2/gnss_status_adapter.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"
#include "universal_gnss_ros2/msg/rtcm_frame.hpp"

namespace universal_gnss_ros2 {

namespace {

using SteadyClock = std::chrono::steady_clock;

struct NtripNodeConfig
{
  universal_gnss_ntrip::NtripConfig ntrip{};
  universal_gnss_transport::TcpClientConfig tcp{};
  bool tls_enabled{false};
  double rtcm_forwarding_activity_timeout_s{5.0};
  std::string expected_source_incarnation{};
};

[[noreturn]] void ThrowInvalidParameter(rclcpp::Node& node, const std::string& parameter_name,
                                        const std::string& message)
{
  const std::string full_message = "Invalid parameter '" + parameter_name + "': " + message;
  RCLCPP_ERROR(node.get_logger(), "%s", full_message.c_str());
  throw std::invalid_argument(full_message);
}

const char* ToString(const universal_gnss_ntrip::NtripClientError error)
{
  using universal_gnss_ntrip::NtripClientError;

  switch (error)
  {
  case NtripClientError::kNone:
    return "none";
  case NtripClientError::kConfiguration:
    return "configuration";
  case NtripClientError::kAuthentication:
    return "authentication";
  case NtripClientError::kHttp:
    return "http";
  case NtripClientError::kProtocol:
    return "protocol";
  case NtripClientError::kTimeout:
    return "timeout";
  case NtripClientError::kDisconnected:
    return "disconnected";
  case NtripClientError::kUnknown:
  default:
    return "unknown";
  }
}

const char* ToString(const universal_gnss_ntrip::NtripClientState state)
{
  using universal_gnss_ntrip::NtripClientState;

  switch (state)
  {
  case NtripClientState::kDisconnected:
    return "disconnected";
  case NtripClientState::kConnecting:
    return "connecting";
  case NtripClientState::kConnected:
    return "connected";
  case NtripClientState::kStreaming:
    return "streaming";
  case NtripClientState::kFailed:
  default:
    return "failed";
  }
}

const char* ToString(const universal_gnss_ntrip::NtripGgaSendError error)
{
  using universal_gnss_ntrip::NtripGgaSendError;

  switch (error)
  {
  case NtripGgaSendError::kGenerationFailed:
    return "generation_failed";
  case NtripGgaSendError::kDisconnected:
    return "disconnected";
  case NtripGgaSendError::kTimeout:
    return "timeout";
  case NtripGgaSendError::kWriteFailure:
  default:
    return "write_failure";
  }
}

universal_gnss::GnssDiagnosticEvent MakeEvent(universal_gnss::GnssDiagnosticSeverity severity,
                                              universal_gnss::GnssDiagnosticCategory category,
                                              std::string code, std::string message)
{
  universal_gnss::GnssDiagnosticEvent event;
  event.severity = severity;
  event.category = category;
  event.code = std::move(code);
  event.message = std::move(message);
  event.source = "ntrip_node";
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
  return std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now().time_since_epoch())
      .count();
}

std::int64_t RosTimestampNs(const builtin_interfaces::msg::Time& stamp)
{
  return static_cast<std::int64_t>(stamp.sec) * 1000000000LL +
         static_cast<std::int64_t>(stamp.nanosec);
}

std::uint32_t LoadTimeoutMilliseconds(rclcpp::Node& node, const std::string& parameter_name,
                                      const double default_seconds)
{
  const double seconds = node.declare_parameter<double>(parameter_name, default_seconds);
  constexpr double kMillisecondsPerSecond = 1000.0;
  const double max_seconds =
      static_cast<double>(std::numeric_limits<std::uint32_t>::max()) / kMillisecondsPerSecond;
  const double milliseconds = std::ceil(seconds * kMillisecondsPerSecond);
  if (!std::isfinite(seconds) || seconds < 0.0 || seconds > max_seconds ||
      milliseconds > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
  {
    ThrowInvalidParameter(
        node, parameter_name,
        "must be finite, non-negative, and representable in milliseconds (zero disables it)");
  }

  return static_cast<std::uint32_t>(milliseconds);
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

std::string BuildHardwareId(const NtripNodeConfig& config)
{
  std::ostringstream stream;
  stream << config.ntrip.host << ':' << config.ntrip.port << '/' << config.ntrip.mountpoint;
  return stream.str();
}

NtripNodeConfig LoadNtripNodeConfig(rclcpp::Node& node)
{
  NtripNodeConfig config;

  config.ntrip.host = node.declare_parameter<std::string>("caster_host", "");
  const auto caster_port = node.declare_parameter<std::int64_t>("caster_port", 2101);
  config.ntrip.mountpoint = node.declare_parameter<std::string>("mountpoint", "");
  config.ntrip.username = node.declare_parameter<std::string>("username", "");
  config.ntrip.password = node.declare_parameter<std::string>("password", "");
  config.ntrip.send_gga = node.declare_parameter<bool>("gga_enabled", false);
  const auto gga_interval_s = node.declare_parameter<std::int64_t>("gga_interval_s", 10);
  config.tls_enabled = node.declare_parameter<bool>("tls_enabled", false);
  config.rtcm_forwarding_activity_timeout_s =
      node.declare_parameter<double>("rtcm_forwarding_activity_timeout_s", 5.0);
  config.expected_source_incarnation =
      node.declare_parameter<std::string>("expected_source_incarnation", "");
  config.ntrip.first_rtcm_frame_timeout_ms =
      LoadTimeoutMilliseconds(node, "correction_first_frame_timeout_s", 30.0);
  config.ntrip.rtcm_frame_timeout_ms =
      LoadTimeoutMilliseconds(node, "correction_inter_frame_timeout_s", 30.0);
  const auto operational_min_valid_rtcm_frames =
      node.declare_parameter<std::int64_t>("correction_operational_min_valid_frames", 2);

  if (config.ntrip.host.empty())
  {
    ThrowInvalidParameter(node, "caster_host", "must not be empty");
  }

  if (caster_port <= 0 || caster_port > 65535)
  {
    ThrowInvalidParameter(node, "caster_port", "must be in the 1..65535 range");
  }
  config.ntrip.port = static_cast<std::uint16_t>(caster_port);

  if (config.ntrip.mountpoint.empty())
  {
    ThrowInvalidParameter(node, "mountpoint", "must not be empty");
  }

  if (gga_interval_s <= 0 || gga_interval_s > 86400)
  {
    ThrowInvalidParameter(node, "gga_interval_s", "must be in the 1..86400 range");
  }
  config.ntrip.gga_interval_s = static_cast<std::uint32_t>(gga_interval_s);

  if (operational_min_valid_rtcm_frames <= 0 || operational_min_valid_rtcm_frames > 1000000)
  {
    ThrowInvalidParameter(node, "correction_operational_min_valid_frames",
                          "must be in the 1..1000000 range");
  }
  config.ntrip.operational_min_valid_rtcm_frames =
      static_cast<std::uint32_t>(operational_min_valid_rtcm_frames);

  if (config.tls_enabled)
  {
    ThrowInvalidParameter(node, "tls_enabled",
                          "TLS is not supported by the current low-level NTRIP transport");
  }

  if (!std::isfinite(config.rtcm_forwarding_activity_timeout_s) ||
      !(config.rtcm_forwarding_activity_timeout_s > 0.0))
  {
    ThrowInvalidParameter(node, "rtcm_forwarding_activity_timeout_s",
                          "must be finite and strictly positive");
  }

  config.tcp.host = config.ntrip.host;
  config.tcp.port = config.ntrip.port;
  config.tcp.connect_timeout_ms = 1000u;
  config.tcp.read_timeout_ms = 0u;
  config.tcp.write_timeout_ms = 0u;
  config.tcp.nonblocking = true;

  return config;
}

} // namespace

struct NtripNode::Impl
{
  static constexpr std::chrono::milliseconds kPollPeriod{200};
  static constexpr std::chrono::seconds kGnssInputGracePeriod{3};
  static constexpr std::chrono::seconds kGnssInputStaleTimeout{5};
  static constexpr universal_gnss_protocols::ProtocolTimestampNs kCorrectionStaleAfterNs =
      5000000000LL;
  static constexpr universal_gnss_protocols::ProtocolTimestampNs kCorrectionRequirementWindowNs =
      30000000000LL;
  static constexpr universal_gnss_protocols::ProtocolTimestampNs
      kCorrectionRequirementStartupGraceNs = 30000000000LL;

  explicit Impl(NtripNode& owner, std::optional<int> adopted_socket_fd) : owner_(owner)
  {
    startup_events_.reserve(8u);
    config_ = LoadNtripNodeConfig(owner_);
    hardware_id_ = BuildHardwareId(config_);

    diagnostics_publisher_ =
        owner_.create_publisher<diagnostic_msgs::msg::DiagnosticArray>("diagnostics", 10);
    health_service_ = owner_.create_service<std_srvs::srv::Trigger>(
        "~/get_health", [](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
                           std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
          response->success = true;
          response->message = "component=ntrip_node responsive=true";
        });
    rtcm_publisher_ = owner_.create_publisher<universal_gnss_ros2::msg::RtcmFrame>(
        "rtcm", rclcpp::QoS(rclcpp::KeepLast(50)).reliable());
    status_subscription_ = owner_.create_subscription<universal_gnss_ros2::msg::GnssStatus>(
        "status", 10, [this](const universal_gnss_ros2::msg::GnssStatus& message) {
          this->OnStatusMessage(message);
        });

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
    client_.emplace(config_.ntrip);
    client_->set_tcp_config(config_.tcp);
    client_ready_ = true;
    if (adopted_socket_fd.has_value())
    {
      const auto error = client_->AdoptConnectedSocket(*adopted_socket_fd);
      if (error != universal_gnss_ntrip::NtripClientError::kNone)
      {
        client_ready_ = false;
        startup_events_.push_back(MakeEvent(
            universal_gnss::GnssDiagnosticSeverity::kError,
            universal_gnss::GnssDiagnosticCategory::kTransport, "ntrip_socket_adopt_failed",
            "Failed to adopt connected NTRIP test socket: " + std::string(ToString(error))));
      } else
      {
        initial_connect_attempted_ = true;
      }
    }
#else
    (void)adopted_socket_fd;
    startup_events_.push_back(
        MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                  universal_gnss::GnssDiagnosticCategory::kTransport, "ntrip_transport_unavailable",
                  "TCP-backed NTRIP transport is unavailable on this platform"));
#endif

    for (const auto& event : startup_events_)
    {
      LogDiagnosticEvent(owner_, event);
    }

    timer_ = owner_.create_wall_timer(kPollPeriod, [this]() { this->OnTimer(); });
  }

  void OnTimer()
  {
    StepOnce();
    PublishNow();
  }

  void OnStatusMessage(const universal_gnss_ros2::msg::GnssStatus& message)
  {
    const auto source = std::make_tuple(message.source_id, message.source_incarnation);
    const bool has_partial_source = message.source_id.empty() != message.source_incarnation.empty();
    const bool has_unexpected_incarnation =
        !config_.expected_source_incarnation.empty() &&
        message.source_incarnation != config_.expected_source_incarnation;
    if (has_partial_source || has_unexpected_incarnation ||
        (active_source_.has_value() && source != *active_source_))
    {
      ++source_conflicts_;
      return;
    }
    if (!active_source_.has_value())
    {
      active_source_ = source;
      runtime_state_.reset();
      last_position_observation_time_.reset();
      last_position_observation_sequence_.reset();
      last_legacy_position_stamp_ns_.reset();
    }

    runtime_state_ = FromGnssStatusMessage(message);

    bool is_new_position_observation = false;
    if (message.position_observation_sequence != 0u)
    {
      is_new_position_observation =
          !last_position_observation_sequence_.has_value() ||
          message.position_observation_sequence != *last_position_observation_sequence_;
      last_position_observation_sequence_ = message.position_observation_sequence;
      last_legacy_position_stamp_ns_.reset();
    } else
    {
      const std::int64_t stamp_ns = RosTimestampNs(message.stamp);
      is_new_position_observation = !received_status_ ||
                                    !last_legacy_position_stamp_ns_.has_value() ||
                                    stamp_ns != *last_legacy_position_stamp_ns_;
      last_legacy_position_stamp_ns_ = stamp_ns;
      last_position_observation_sequence_.reset();
    }

    received_status_ = true;
    if (is_new_position_observation)
    {
      last_position_observation_time_ = SteadyClock::now();
    }
  }

  bool StepOnce()
  {
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
    if (!client_ready_ || !client_.has_value())
    {
      return false;
    }

    bool advanced = false;
    const auto now_ns = static_cast<universal_gnss::GnssTimestampNs>(MonotonicNowNs());

    advanced = EnsureConnected(now_ns) || advanced;
    advanced = EnsureRequestSent(now_ns) || advanced;
    advanced = ReadOnce(now_ns) || advanced;
    advanced = MaybeInjectGga(now_ns) || advanced;
    return advanced;
#else
    return false;
#endif
  }

  void PublishNow()
  {
    auto health = BuildHealthSummary();
    last_diagnostics_message_ =
        ToDiagnosticArrayMessage(health, "universal_gnss_ntrip", hardware_id_);
    last_diagnostics_message_->header.stamp = ToRosTime(
        std::optional<universal_gnss::GnssTimestampNs>(owner_.get_clock()->now().nanoseconds()));
    AppendRtcmForwardingStatus(*last_diagnostics_message_);
    if (client_.has_value())
    {
      AppendRtcmSemanticObservationStatuses(
          *last_diagnostics_message_,
          universal_gnss_protocols::BuildRtcmSemanticObservations(
              client_->correction_monitor(),
              static_cast<universal_gnss_protocols::ProtocolTimestampNs>(MonotonicNowNs())),
          "universal_gnss_ntrip", hardware_id_);
    }
    diagnostics_publisher_->publish(*last_diagnostics_message_);
  }

  bool diagnostics_ready() const
  {
    return diagnostics_publisher_ != nullptr && status_subscription_ != nullptr &&
           rtcm_publisher_ != nullptr;
  }

  bool has_runtime_state() const { return runtime_state_.has_value(); }

  bool EnsureConnected(const universal_gnss::GnssTimestampNs now_ns)
  {
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
    if (!client_.has_value())
    {
      return false;
    }

    const auto state = client_->state();
    if (state == universal_gnss_ntrip::NtripClientState::kConnected ||
        state == universal_gnss_ntrip::NtripClientState::kStreaming)
    {
      return false;
    }

    if (!initial_connect_attempted_)
    {
      initial_connect_attempted_ = true;
      return AttemptConnect(now_ns);
    }

    if (state == universal_gnss_ntrip::NtripClientState::kFailed &&
        config_.ntrip.reconnect_policy.ShouldReconnect(client_->reconnect_state(), now_ns))
    {
      return AttemptConnect(now_ns);
    }

    return false;
#else
    (void)now_ns;
    return false;
#endif
  }

  bool EnsureRequestSent(const universal_gnss::GnssTimestampNs now_ns)
  {
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
    if (!client_.has_value())
    {
      return false;
    }

    if (client_->state() != universal_gnss_ntrip::NtripClientState::kConnected ||
        client_->metrics().request_sent)
    {
      return false;
    }

    const auto error = client_->SendRequest(now_ns);
    if (error != universal_gnss_ntrip::NtripClientError::kNone)
    {
      LogClientTransition(client_->state(), error);
      return false;
    }

    return true;
#else
    (void)now_ns;
    return false;
#endif
  }

  bool ReadOnce(const universal_gnss::GnssTimestampNs now_ns)
  {
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
    if (!client_.has_value())
    {
      return false;
    }

    if (!client_->metrics().request_sent ||
        (client_->state() != universal_gnss_ntrip::NtripClientState::kConnected &&
         client_->state() != universal_gnss_ntrip::NtripClientState::kStreaming))
    {
      return false;
    }

    bool advanced = false;
    for (std::size_t iteration = 0u; iteration < 8u; ++iteration)
    {
      std::uint8_t buffer[4096] = {};
      std::vector<universal_gnss_protocols::RtcmFrame> observed_frames;
      const auto state_before = client_->state();
      const auto read_result = client_->Read(buffer, sizeof(buffer), now_ns, &observed_frames);

      if (read_result.bytes_read > 0u)
      {
        advanced = true;
        const auto public_receipt_stamp =
            ToRosTime(std::optional<universal_gnss::GnssTimestampNs>(owner_.now().nanoseconds()));

        for (const auto& frame : observed_frames)
        {
          universal_gnss_ros2::msg::RtcmFrame message;
          message.stamp = public_receipt_stamp;
          message.message_type = frame.message_type;
          message.data = frame.raw_bytes;
          last_rtcm_message_ = message;
          rtcm_publisher_->publish(message);
          ++rtcm_published_frames_;
          last_rtcm_published_time_ = SteadyClock::now();
          last_rtcm_message_type_ = frame.message_type;
        }
        continue;
      }

      if (read_result.client_error != universal_gnss_ntrip::NtripClientError::kNone)
      {
        LogClientTransition(client_->state(), read_result.client_error);
        break;
      }

      if (state_before != universal_gnss_ntrip::NtripClientState::kStreaming &&
          client_->state() == universal_gnss_ntrip::NtripClientState::kStreaming)
      {
        advanced = true;
        continue;
      }

      break;
    }

    return advanced;
#else
    (void)now_ns;
    return false;
#endif
  }

  bool MaybeInjectGga(const universal_gnss::GnssTimestampNs now_ns)
  {
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
    if (!client_.has_value() || !runtime_state_.has_value())
    {
      return false;
    }

    if (last_position_observation_time_.has_value() &&
        SteadyClock::now() - *last_position_observation_time_ >= kGnssInputStaleTimeout)
    {
      return false;
    }

    const auto result = client_->MaybeInjectGga(*runtime_state_, now_ns);
    last_gga_result_ = result;
    if (!result.ok() && result.client_error != universal_gnss_ntrip::NtripClientError::kNone)
    {
      LogClientTransition(client_->state(), result.client_error);
    }

    return result.sent();
#else
    (void)now_ns;
    return false;
#endif
  }

  bool AttemptConnect(const universal_gnss::GnssTimestampNs now_ns)
  {
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
    const auto error = client_->Connect(now_ns);
    if (error != universal_gnss_ntrip::NtripClientError::kNone)
    {
      LogClientTransition(client_->state(), error);
      return false;
    }

    return true;
#else
    (void)now_ns;
    return false;
#endif
  }

  universal_gnss_protocols::RtcmCorrectionHealthOptions
  BuildCorrectionHealthOptions(const universal_gnss::GnssTimestampNs now_ns) const
  {
    universal_gnss_protocols::RtcmCorrectionHealthOptions options;
    options.now_timestamp_ns = now_ns;
    options.stale_after_ns = kCorrectionStaleAfterNs;
    options.required_observation_window_ns = kCorrectionRequirementWindowNs;
    options.startup_grace_ns = kCorrectionRequirementStartupGraceNs;
    universal_gnss_protocols::ConfigurePortableRtkCorrectionRequirements(options);
    return options;
  }

  bool HasRecentRtcmPublication(const SteadyClock::time_point now) const
  {
    if (!last_rtcm_published_time_.has_value())
    {
      return false;
    }

    const auto timeout = std::chrono::duration_cast<SteadyClock::duration>(
        std::chrono::duration<double>(config_.rtcm_forwarding_activity_timeout_s));
    return now - *last_rtcm_published_time_ < timeout;
  }

  universal_gnss::GnssHealthSummary BuildHealthSummary() const
  {
    universal_gnss::GnssHealthSummary summary;
    summary.overall_severity = universal_gnss::GnssDiagnosticSeverity::kOk;

    if (runtime_state_.has_value())
    {
      summary.fix_valid = runtime_state_->fix_valid;
      summary.rtk_available = HasRtkAvailability(*runtime_state_);
    }

    for (const auto& event : startup_events_)
    {
      summary.AddEvent(event);
    }

    if (!client_ready_ || !client_.has_value())
    {
      summary.transport_healthy = false;
      summary.receiver_healthy = false;
      summary.parser_healthy = false;
      return summary;
    }

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
    const auto now_ns = static_cast<universal_gnss::GnssTimestampNs>(MonotonicNowNs());
    const auto now = SteadyClock::now();
    const auto client_state = client_->state();
    const auto& metrics = client_->metrics();

    summary.transport_healthy =
        client_state == universal_gnss_ntrip::NtripClientState::kConnected ||
        client_state == universal_gnss_ntrip::NtripClientState::kStreaming;
    summary.receiver_healthy = summary.transport_healthy;
    summary.parser_healthy = metrics.invalid_rtcm_frames == 0u;

    switch (client_state)
    {
    case universal_gnss_ntrip::NtripClientState::kDisconnected:
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                 universal_gnss::GnssDiagnosticCategory::kTransport,
                                 "ntrip_disconnected", "NTRIP client is disconnected"));
      break;

    case universal_gnss_ntrip::NtripClientState::kConnected:
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kInfo,
                                 universal_gnss::GnssDiagnosticCategory::kTransport,
                                 "ntrip_connected",
                                 "NTRIP TCP connection is open and awaiting stream data"));
      break;

    case universal_gnss_ntrip::NtripClientState::kStreaming:
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kOk,
                                 universal_gnss::GnssDiagnosticCategory::kTransport,
                                 "ntrip_streaming", "NTRIP response stream is open"));
      break;

    case universal_gnss_ntrip::NtripClientState::kFailed: {
      if (config_.ntrip.reconnect_policy.CanAttempt(client_->reconnect_state()) &&
          client_->reconnect_state().next_attempt_time_ns.has_value())
      {
        std::ostringstream message;
        message << "NTRIP client is reconnecting after " << ToString(metrics.last_error);
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                   universal_gnss::GnssDiagnosticCategory::kTransport,
                                   "ntrip_reconnecting", message.str()));
      } else
      {
        summary.AddEvent(
            MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                      universal_gnss::GnssDiagnosticCategory::kTransport, "ntrip_failed",
                      "NTRIP client failed: " + std::string(ToString(metrics.last_error))));
      }
      break;
    }

    case universal_gnss_ntrip::NtripClientState::kConnecting:
    default:
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kInfo,
                                 universal_gnss::GnssDiagnosticCategory::kTransport,
                                 "ntrip_connecting", "NTRIP client is connecting"));
      break;
    }

    if (!metrics.response_received &&
        client_state == universal_gnss_ntrip::NtripClientState::kConnected)
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kInfo,
                                 universal_gnss::GnssDiagnosticCategory::kTransport,
                                 "ntrip_waiting_response",
                                 "NTRIP request sent; waiting for caster response header"));
    }

    if (metrics.invalid_rtcm_frames > 0u)
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                 universal_gnss::GnssDiagnosticCategory::kParser,
                                 "rtcm_invalid_frames",
                                 "Invalid RTCM frames were observed in the correction stream"));
    }

    if (client_state == universal_gnss_ntrip::NtripClientState::kStreaming)
    {
      const auto& correction_flow = client_->correction_flow_state();
      if (correction_flow.valid_rtcm_frames == 0u)
      {
        summary.AddEvent(MakeEvent(
            universal_gnss::GnssDiagnosticSeverity::kInfo,
            universal_gnss::GnssDiagnosticCategory::kCorrection, "correction_stream_waiting",
            "Correction stream is connected but no RTCM frames have been observed yet"));
      } else
      {
        if (client_->IsCorrectionFlowing())
        {
          summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kOk,
                                     universal_gnss::GnssDiagnosticCategory::kCorrection,
                                     "correction_flowing",
                                     "Complete integrity-valid RTCM frames are arriving"));
        }
        auto correction_health =
            client_->BuildCorrectionHealth(BuildCorrectionHealthOptions(now_ns));
        summary.correction_available = correction_health.correction_available;
        summary.stale_data = summary.stale_data || correction_health.stale_data;
        summary.parser_healthy = summary.parser_healthy && correction_health.parser_healthy;
        for (const auto& event : correction_health.events)
        {
          summary.AddEvent(event);
        }
      }
    }

    if (HasRecentRtcmPublication(now))
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kOk,
                                 universal_gnss::GnssDiagnosticCategory::kCorrection,
                                 "rtcm_forwarding_active",
                                 "RTCM frames are being published for live receiver forwarding"));
    } else if (last_rtcm_published_time_.has_value())
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                 universal_gnss::GnssDiagnosticCategory::kCorrection,
                                 "rtcm_forwarding_stale",
                                 "RTCM forwarding has not published a frame recently"));
    }

    if (config_.ntrip.send_gga)
    {
      if (!runtime_state_.has_value())
      {
        if (now - startup_time_ >= kGnssInputGracePeriod)
        {
          summary.AddEvent(
              MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                        universal_gnss::GnssDiagnosticCategory::kTiming, "gga_source_missing",
                        "GGA injection is enabled but no GNSS status has been received yet"));
        }
      } else if (last_position_observation_time_.has_value() &&
                 now - *last_position_observation_time_ >= kGnssInputStaleTimeout)
      {
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kStale,
                                   universal_gnss::GnssDiagnosticCategory::kTiming,
                                   "gga_source_stale",
                                   "GNSS status input for NTRIP GGA injection is stale"));
      }

      if (metrics.gga_sent_count > 0u)
      {
        summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kOk,
                                   universal_gnss::GnssDiagnosticCategory::kCorrection,
                                   "gga_injection_active", "NTRIP GGA injection is active"));
      } else if (client_state != universal_gnss_ntrip::NtripClientState::kStreaming)
      {
        summary.AddEvent(
            MakeEvent(universal_gnss::GnssDiagnosticSeverity::kInfo,
                      universal_gnss::GnssDiagnosticCategory::kCorrection, "gga_waiting_stream",
                      "NTRIP GGA injection is enabled and waiting for a streaming connection"));
      }

      if (metrics.last_gga_error.has_value())
      {
        const bool transportish =
            *metrics.last_gga_error == universal_gnss_ntrip::NtripGgaSendError::kTimeout ||
            *metrics.last_gga_error == universal_gnss_ntrip::NtripGgaSendError::kDisconnected;
        summary.AddEvent(MakeEvent(
            transportish ? universal_gnss::GnssDiagnosticSeverity::kWarning
                         : universal_gnss::GnssDiagnosticSeverity::kError,
            universal_gnss::GnssDiagnosticCategory::kCorrection, "gga_send_error",
            "NTRIP GGA injection error: " + std::string(ToString(*metrics.last_gga_error))));
      }
    }

    if (source_conflicts_ > 0u)
    {
      summary.AddEvent(
          MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                    universal_gnss::GnssDiagnosticCategory::kConfiguration, "gga_source_conflict",
                    "GNSS status from a competing or incomplete source incarnation was rejected"));
    }

    return summary;
#else
    return summary;
#endif
  }

  void AppendRtcmForwardingStatus(diagnostic_msgs::msg::DiagnosticArray& diagnostics) const
  {
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = "universal_gnss_ntrip/rtcm_forwarding";
    status.hardware_id = hardware_id_;

    const auto now = SteadyClock::now();
    if (HasRecentRtcmPublication(now))
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
      status.message = "RTCM forwarding active";
    } else if (last_rtcm_published_time_.has_value())
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
      status.message = "RTCM forwarding stale";
    } else if (client_.has_value() &&
               client_->state() == universal_gnss_ntrip::NtripClientState::kStreaming)
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
      status.message = "RTCM forwarding waiting for valid frames";
    } else
    {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
      status.message = "RTCM forwarding idle";
    }

    status.values.push_back(
        MakeKeyValue("published_frame_count", std::to_string(rtcm_published_frames_)));
    if (last_rtcm_message_type_.has_value())
    {
      status.values.push_back(
          MakeKeyValue("last_message_type", std::to_string(*last_rtcm_message_type_)));
    }
    if (last_rtcm_published_time_.has_value())
    {
      const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          SteadyClock::now() - *last_rtcm_published_time_);
      std::ostringstream stream;
      stream << (static_cast<double>(age_ms.count()) / 1000.0);
      status.values.push_back(MakeKeyValue("last_frame_age_s", stream.str()));
    }

    diagnostics.status.push_back(std::move(status));
  }

  void LogClientTransition(const universal_gnss_ntrip::NtripClientState state,
                           const universal_gnss_ntrip::NtripClientError error)
  {
    if (last_logged_state_.has_value() && *last_logged_state_ == state &&
        last_logged_error_ == error)
    {
      return;
    }

    last_logged_state_ = state;
    last_logged_error_ = error;

    if (error == universal_gnss_ntrip::NtripClientError::kNone)
    {
      RCLCPP_INFO(owner_.get_logger(), "NTRIP client state=%s", ToString(state));
      return;
    }

    if (state == universal_gnss_ntrip::NtripClientState::kFailed)
    {
      RCLCPP_WARN(owner_.get_logger(), "NTRIP client state=%s error=%s", ToString(state),
                  ToString(error));
      return;
    }

    RCLCPP_INFO(owner_.get_logger(), "NTRIP client state=%s error=%s", ToString(state),
                ToString(error));
  }

  NtripNode& owner_;
  NtripNodeConfig config_{};
  std::string hardware_id_{};
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_publisher_{};
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr health_service_{};
  rclcpp::Publisher<universal_gnss_ros2::msg::RtcmFrame>::SharedPtr rtcm_publisher_{};
  rclcpp::Subscription<universal_gnss_ros2::msg::GnssStatus>::SharedPtr status_subscription_{};
  rclcpp::TimerBase::SharedPtr timer_{};
  std::vector<universal_gnss::GnssDiagnosticEvent> startup_events_{};
  std::optional<diagnostic_msgs::msg::DiagnosticArray> last_diagnostics_message_{};
  std::optional<universal_gnss_ros2::msg::RtcmFrame> last_rtcm_message_{};
  std::optional<universal_gnss::GnssRuntimeState> runtime_state_{};
  std::optional<std::tuple<std::string, std::string>> active_source_{};
  std::optional<SteadyClock::time_point> last_position_observation_time_{};
  std::optional<std::uint64_t> last_position_observation_sequence_{};
  std::optional<std::int64_t> last_legacy_position_stamp_ns_{};
  std::size_t source_conflicts_{0u};
  std::optional<SteadyClock::time_point> last_rtcm_published_time_{};
  std::optional<universal_gnss_ntrip::NtripGgaSendResult> last_gga_result_{};
  std::optional<std::uint16_t> last_rtcm_message_type_{};
  SteadyClock::time_point startup_time_{SteadyClock::now()};
  std::optional<universal_gnss_ntrip::NtripClientState> last_logged_state_{};
  universal_gnss_ntrip::NtripClientError last_logged_error_{
      universal_gnss_ntrip::NtripClientError::kNone};
  std::size_t rtcm_published_frames_{0u};
  bool received_status_{false};
  bool client_ready_{false};
  bool initial_connect_attempted_{false};

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
  std::optional<universal_gnss_ntrip::NtripClient> client_{};
#endif
};

NtripNode::NtripNode(const rclcpp::NodeOptions& options)
    : rclcpp::Node("universal_gnss_ntrip", options),
      impl_(std::make_unique<Impl>(*this, std::nullopt))
{
}

NtripNode::NtripNode(const int adopted_socket_fd, const rclcpp::NodeOptions& options)
    : rclcpp::Node("universal_gnss_ntrip", options),
      impl_(std::make_unique<Impl>(*this, std::optional<int>(adopted_socket_fd)))
{
}

NtripNode::~NtripNode() = default;

bool NtripNode::StepOnce() { return impl_->StepOnce(); }

void NtripNode::PublishNow() { impl_->PublishNow(); }

bool NtripNode::client_ready() const { return impl_->client_ready_; }

bool NtripNode::diagnostics_ready() const { return impl_->diagnostics_ready(); }

bool NtripNode::has_runtime_state() const { return impl_->has_runtime_state(); }

const std::optional<diagnostic_msgs::msg::DiagnosticArray>&
NtripNode::last_diagnostics_message() const
{
  return impl_->last_diagnostics_message_;
}

const std::optional<universal_gnss_ros2::msg::RtcmFrame>& NtripNode::last_rtcm_message() const
{
  return impl_->last_rtcm_message_;
}

} // namespace universal_gnss_ros2
