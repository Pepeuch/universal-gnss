#include "universal_gnss_ros2/replay_node.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
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
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "universal_gnss/gnss_capabilities.hpp"
#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss/gnss_types.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/protocol_type.hpp"
#include "universal_gnss_ros2/diagnostic_adapter.hpp"
#include "universal_gnss_ros2/gnss_status_adapter.hpp"
#include "universal_gnss_ros2/msg/rtcm_frame.hpp"
#include "universal_gnss_ros2/navsat_fix_adapter.hpp"
#include "universal_gnss_tools/gnss_replay.hpp"

namespace universal_gnss_ros2
{

namespace
{

enum class ReplayMode : std::uint8_t
{
  kStepped = 0,
  kWallTime = 1,
  kFast = 2,
};

struct ReplayNodeConfig
{
  std::string input_path{};
  std::string frame_id{"gnss"};
  ReplayMode replay_mode{ReplayMode::kWallTime};
  bool publish_rtcm{true};
  double wall_time_scale{1.0};
  std::uint32_t fallback_step_ms{100u};
  std::uint32_t timer_poll_ms{1u};
};

struct ReplayAction
{
  std::size_t event_index{0u};
  std::optional<universal_gnss::GnssTimestampNs> timestamp_ns{};
  std::optional<universal_gnss::GnssRuntimeState> state{};
  std::optional<universal_gnss_ros2::msg::RtcmFrame> rtcm{};
};

using SteadyClock = std::chrono::steady_clock;

std::string ToLowerCopy(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

const char* ToString(const ReplayMode mode)
{
  switch (mode)
  {
    case ReplayMode::kStepped:
      return "stepped";
    case ReplayMode::kWallTime:
      return "wall_time";
    case ReplayMode::kFast:
      return "fast";
  }

  return "wall_time";
}

ReplayMode ParseReplayMode(const std::string& mode_text)
{
  const std::string normalized = ToLowerCopy(mode_text);
  if (normalized == "stepped")
  {
    return ReplayMode::kStepped;
  }
  if (normalized == "wall_time")
  {
    return ReplayMode::kWallTime;
  }
  if (normalized == "fast")
  {
    return ReplayMode::kFast;
  }

  throw std::invalid_argument(
      "Invalid parameter 'replay_mode': expected one of: stepped, wall_time, fast");
}

[[noreturn]] void ThrowInvalidParameter(rclcpp::Node& node,
                                        const std::string& parameter_name,
                                        const std::string& message)
{
  const std::string full_message = "Invalid parameter '" + parameter_name + "': " + message;
  RCLCPP_ERROR(node.get_logger(), "%s", full_message.c_str());
  throw std::invalid_argument(full_message);
}

std::vector<std::uint8_t> ReadBinaryFile(const std::string& path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input)
  {
    throw std::runtime_error("failed to open replay input: " + path);
  }

  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  return std::vector<std::uint8_t>(contents.begin(), contents.end());
}

std::string BasenameOf(const std::string& path)
{
  const auto slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1u);
}

bool IsChecksumAccepted(const universal_gnss_protocols::ChecksumStatus status)
{
  return status != universal_gnss_protocols::ChecksumStatus::kInvalid;
}

bool IsRuntimeProtocol(const universal_gnss_protocols::ProtocolType protocol)
{
  return protocol == universal_gnss_protocols::ProtocolType::kNmea ||
         protocol == universal_gnss_protocols::ProtocolType::kUbx ||
         protocol == universal_gnss_protocols::ProtocolType::kUnicore;
}

bool ParseUnsigned16Text(const std::string& text, std::uint16_t& value)
{
  try
  {
    std::size_t consumed = 0u;
    const auto parsed = std::stoul(text, &consumed, 10);
    if (consumed != text.size() ||
        parsed > static_cast<unsigned long>(std::numeric_limits<std::uint16_t>::max()))
    {
      return false;
    }

    value = static_cast<std::uint16_t>(parsed);
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

std::optional<universal_gnss::GnssTimestampNs> SelectReplayTimestamp(
    const universal_gnss_tools::GnssReplayEvent& event,
    const std::optional<universal_gnss::GnssTimestampNs>& last_known_timestamp)
{
  if (event.state_after_event.timestamp_ns.has_value())
  {
    return event.state_after_event.timestamp_ns;
  }

  return last_known_timestamp;
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

bool CanPublishFixMessage(const universal_gnss::GnssRuntimeState& state)
{
  if (!state.latitude_deg.has_value() || !state.longitude_deg.has_value())
  {
    return false;
  }

  return std::isfinite(*state.latitude_deg) && std::isfinite(*state.longitude_deg);
}

universal_gnss::GnssDiagnosticEvent MakeEvent(universal_gnss::GnssDiagnosticSeverity severity,
                                              universal_gnss::GnssDiagnosticCategory category,
                                              std::string code,
                                              std::string message,
                                              std::optional<universal_gnss::GnssTimestampNs> timestamp_ns = std::nullopt)
{
  universal_gnss::GnssDiagnosticEvent event;
  event.severity = severity;
  event.category = category;
  event.code = std::move(code);
  event.message = std::move(message);
  event.timestamp_ns = timestamp_ns;
  event.source = "replay_node";
  return event;
}

diagnostic_msgs::msg::KeyValue MakeKeyValue(std::string key, std::string value)
{
  diagnostic_msgs::msg::KeyValue entry;
  entry.key = std::move(key);
  entry.value = std::move(value);
  return entry;
}

ReplayNodeConfig LoadReplayNodeConfig(rclcpp::Node& node)
{
  ReplayNodeConfig config;
  config.input_path = node.declare_parameter<std::string>("input_path", "");
  config.frame_id = node.declare_parameter<std::string>("frame_id", "gnss");
  config.publish_rtcm = node.declare_parameter<bool>("publish_rtcm", true);
  config.replay_mode =
      ParseReplayMode(node.declare_parameter<std::string>("replay_mode", "wall_time"));
  config.wall_time_scale = node.declare_parameter<double>("wall_time_scale", 1.0);

  const auto fallback_step_ms = node.declare_parameter<std::int64_t>("fallback_step_ms", 100);
  const auto timer_poll_ms = node.declare_parameter<std::int64_t>("timer_poll_ms", 1);

  if (config.input_path.empty())
  {
    ThrowInvalidParameter(node, "input_path", "must be set to a replay log path");
  }

  if (config.frame_id.empty())
  {
    ThrowInvalidParameter(node, "frame_id", "must not be empty");
  }

  if (!std::isfinite(config.wall_time_scale) || !(config.wall_time_scale > 0.0))
  {
    ThrowInvalidParameter(node, "wall_time_scale", "must be finite and strictly positive");
  }

  if (fallback_step_ms <= 0 ||
      fallback_step_ms >
          static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
  {
    ThrowInvalidParameter(node, "fallback_step_ms", "must be in the 1..4294967295 range");
  }
  config.fallback_step_ms = static_cast<std::uint32_t>(fallback_step_ms);

  if (timer_poll_ms <= 0 ||
      timer_poll_ms >
          static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
  {
    ThrowInvalidParameter(node, "timer_poll_ms", "must be in the 1..4294967295 range");
  }
  config.timer_poll_ms = static_cast<std::uint32_t>(timer_poll_ms);

  return config;
}

}  // namespace

struct ReplayNode::Impl
{
  explicit Impl(ReplayNode& owner) : owner_(owner)
  {
    config_ = LoadReplayNodeConfig(owner_);
    hardware_id_ = "replay:" + BasenameOf(config_.input_path);

    status_publisher_ =
        owner_.create_publisher<universal_gnss_ros2::msg::GnssStatus>("status", 10);
    fix_publisher_ = owner_.create_publisher<sensor_msgs::msg::NavSatFix>("fix", 10);
    diagnostics_publisher_ =
        owner_.create_publisher<diagnostic_msgs::msg::DiagnosticArray>("diagnostics", 10);
    if (config_.publish_rtcm)
    {
      rtcm_publisher_ = owner_.create_publisher<universal_gnss_ros2::msg::RtcmFrame>(
          "rtcm", rclcpp::QoS(rclcpp::KeepLast(50)).reliable());
    }

    step_service_ = owner_.create_service<std_srvs::srv::Trigger>(
        "~/step",
        [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
               std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
          const bool advanced = this->StepOnce();
          response->success = advanced;
          if (advanced)
          {
            std::ostringstream stream;
            stream << "advanced replay to action " << processed_actions_ << " of "
                   << actions_.size();
            response->message = stream.str();
          }
          else
          {
            response->message = replay_complete_ ? "replay already complete"
                                                 : "no replay actions are available";
          }
        });

    bytes_ = ReadBinaryFile(config_.input_path);
    replay_result_ = universal_gnss_tools::ReplayGnssBytes(bytes_, true);
    BuildActions();

    if (actions_.empty())
    {
      replay_complete_ = true;
    }

    PublishDiagnosticsNow();

    if (config_.replay_mode != ReplayMode::kStepped && !replay_complete_)
    {
      next_due_time_ = SteadyClock::now();
      timer_ = owner_.create_wall_timer(
          std::chrono::milliseconds(config_.timer_poll_ms), [this]() { this->OnTimer(); });
    }
  }

  void BuildActions()
  {
    std::optional<universal_gnss::GnssTimestampNs> last_known_timestamp{};

    for (const auto& event : replay_result_.events)
    {
      const auto timestamp_ns = SelectReplayTimestamp(event, last_known_timestamp);
      if (event.state_after_event.timestamp_ns.has_value())
      {
        last_known_timestamp = event.state_after_event.timestamp_ns;
        capture_timestamps_available_ = true;
      }

      ReplayAction action;
      action.event_index = event.event_index;
      action.timestamp_ns = timestamp_ns;

      if (IsRuntimeProtocol(event.protocol) && IsChecksumAccepted(event.checksum_status))
      {
        action.state = event.state_after_event;
      }

      if (config_.publish_rtcm &&
          event.protocol == universal_gnss_protocols::ProtocolType::kRtcm3 &&
          IsChecksumAccepted(event.checksum_status) &&
          event.byte_offset + event.length_bytes <= bytes_.size())
      {
        universal_gnss_ros2::msg::RtcmFrame message;
        message.stamp = ToRosTime(timestamp_ns);
        ParseUnsigned16Text(event.identity, message.message_type);
        const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(event.byte_offset);
        const auto end = begin + static_cast<std::ptrdiff_t>(event.length_bytes);
        message.data.assign(begin, end);
        action.rtcm = std::move(message);
      }

      if (action.state.has_value())
      {
        ++total_runtime_actions_;
      }
      if (action.rtcm.has_value())
      {
        ++total_rtcm_actions_;
      }

      if (action.state.has_value() || action.rtcm.has_value())
      {
        actions_.push_back(std::move(action));
      }
    }
  }

  void OnTimer()
  {
    if (config_.replay_mode == ReplayMode::kStepped || replay_complete_ ||
        !next_due_time_.has_value())
    {
      return;
    }

    const auto now = SteadyClock::now();
    std::size_t guard = 0u;
    while (!replay_complete_ && next_due_time_.has_value() && now >= *next_due_time_)
    {
      if (!StepOnce())
      {
        break;
      }

      ++guard;
      if (guard >= actions_.size())
      {
        break;
      }
    }
  }

  std::chrono::nanoseconds DelayToNextAction(const std::size_t current_index) const
  {
    if (config_.replay_mode == ReplayMode::kFast ||
        current_index + 1u >= actions_.size())
    {
      return std::chrono::nanoseconds(0);
    }

    const auto& current = actions_[current_index];
    const auto& next = actions_[current_index + 1u];
    if (!current.timestamp_ns.has_value() || !next.timestamp_ns.has_value() ||
        *next.timestamp_ns < *current.timestamp_ns)
    {
      return std::chrono::milliseconds(config_.fallback_step_ms);
    }

    const auto delta_ns = *next.timestamp_ns - *current.timestamp_ns;
    if (delta_ns <= 0)
    {
      return std::chrono::nanoseconds(0);
    }

    const double scaled_ns =
        static_cast<double>(delta_ns) / config_.wall_time_scale;
    if (!std::isfinite(scaled_ns) || scaled_ns <= 0.0)
    {
      return std::chrono::nanoseconds(0);
    }

    const auto rounded_ns =
        static_cast<long long>(std::llround(scaled_ns));
    if (rounded_ns <= 0)
    {
      return std::chrono::nanoseconds(0);
    }

    return std::chrono::nanoseconds(rounded_ns);
  }

  universal_gnss::GnssHealthSummary BuildHealthSummary() const
  {
    universal_gnss::GnssHealthSummary summary;
    summary.overall_severity = universal_gnss::GnssDiagnosticSeverity::kOk;
    summary.fix_valid = current_state_.fix_valid;
    summary.rtk_available = HasRtkAvailability(current_state_);
    summary.correction_available =
        HasCorrectionAvailability(current_state_) || rtcm_published_frames_ > 0u;
    summary.receiver_healthy =
        !HasKnownBoolField(current_state_,
                           universal_gnss::GnssCapability::kInterferenceState,
                           current_state_.interference_detected) &&
        !HasKnownBoolField(current_state_,
                           universal_gnss::GnssCapability::kJammingState,
                           current_state_.jamming_detected);
    summary.transport_healthy = load_succeeded_;
    summary.parser_healthy = replay_result_.summary.invalid_records == 0u &&
                             replay_result_.summary.malformed_events == 0u;

    const std::optional<universal_gnss::GnssTimestampNs> timestamp_ns =
        has_runtime_state_ ? current_state_.timestamp_ns : std::nullopt;

    summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kInfo,
                               universal_gnss::GnssDiagnosticCategory::kConfiguration,
                               "replay_loaded",
                               "Loaded replay input from " + config_.input_path,
                               timestamp_ns));

    if (replay_result_.summary.invalid_records > 0u)
    {
      summary.AddEvent(MakeEvent(
          universal_gnss::GnssDiagnosticSeverity::kWarning,
          universal_gnss::GnssDiagnosticCategory::kParser,
          "replay_invalid_records",
          "Replay input contains checksum-invalid records",
          timestamp_ns));
    }

    if (replay_result_.summary.malformed_events > 0u)
    {
      summary.AddEvent(MakeEvent(
          universal_gnss::GnssDiagnosticSeverity::kWarning,
          universal_gnss::GnssDiagnosticCategory::kParser,
          "replay_malformed_records",
          "Replay input contains malformed records",
          timestamp_ns));
    }

    if (replay_result_.summary.truncated_records > 0u)
    {
      summary.AddEvent(MakeEvent(
          universal_gnss::GnssDiagnosticSeverity::kWarning,
          universal_gnss::GnssDiagnosticCategory::kParser,
          "replay_truncated_records",
          "Replay input ended with truncated records",
          timestamp_ns));
    }

    if (actions_.empty())
    {
      summary.AddEvent(MakeEvent(
          universal_gnss::GnssDiagnosticSeverity::kWarning,
          universal_gnss::GnssDiagnosticCategory::kRuntime,
          "replay_no_publishable_actions",
          "Replay input did not produce any ROS-visible GNSS or RTCM actions",
          timestamp_ns));
    }

    if (!has_runtime_state_ && !actions_.empty())
    {
      summary.AddEvent(MakeEvent(
          universal_gnss::GnssDiagnosticSeverity::kInfo,
          universal_gnss::GnssDiagnosticCategory::kRuntime,
          "replay_waiting_for_runtime",
          "Replay has not reached the first GNSS runtime update yet",
          timestamp_ns));
    }

    if (rtcm_published_frames_ > 0u)
    {
      summary.AddEvent(MakeEvent(
          universal_gnss::GnssDiagnosticSeverity::kOk,
          universal_gnss::GnssDiagnosticCategory::kCorrection,
          "replay_rtcm_active",
          "Replay is publishing RTCM frames",
          timestamp_ns));
    }

    if (HasKnownBoolField(
            current_state_,
            universal_gnss::GnssCapability::kInterferenceState,
            current_state_.interference_detected))
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kWarning,
                                 universal_gnss::GnssDiagnosticCategory::kReceiver,
                                 "interference_detected",
                                 "Replay state reports RF interference",
                                 timestamp_ns));
    }

    if (HasKnownBoolField(
            current_state_,
            universal_gnss::GnssCapability::kJammingState,
            current_state_.jamming_detected))
    {
      summary.AddEvent(MakeEvent(universal_gnss::GnssDiagnosticSeverity::kError,
                                 universal_gnss::GnssDiagnosticCategory::kReceiver,
                                 "jamming_detected",
                                 "Replay state reports GNSS jamming",
                                 timestamp_ns));
    }

    if (replay_complete_)
    {
      summary.AddEvent(MakeEvent(
          universal_gnss::GnssDiagnosticSeverity::kInfo,
          universal_gnss::GnssDiagnosticCategory::kTiming,
          "replay_complete",
          "Replay has reached end of file",
          timestamp_ns));
    }

    return summary;
  }

  void AppendProgressStatus(diagnostic_msgs::msg::DiagnosticArray& diagnostics) const
  {
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = "universal_gnss_replay/progress";
    status.hardware_id = hardware_id_;
    status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
    status.message = replay_complete_ ? "Replay complete" : "Replay loaded";

    status.values.push_back(MakeKeyValue("input_path", config_.input_path));
    status.values.push_back(MakeKeyValue("replay_mode", ToString(config_.replay_mode)));
    status.values.push_back(
        MakeKeyValue("publish_rtcm", config_.publish_rtcm ? "true" : "false"));
    status.values.push_back(
        MakeKeyValue("capture_timestamps_available",
                     capture_timestamps_available_ ? "true" : "false"));
    status.values.push_back(
        MakeKeyValue("recognized_records",
                     std::to_string(replay_result_.summary.recognized_records)));
    status.values.push_back(
        MakeKeyValue("runtime_updates",
                     std::to_string(replay_result_.summary.runtime_updates)));
    status.values.push_back(
        MakeKeyValue("invalid_records",
                     std::to_string(replay_result_.summary.invalid_records)));
    status.values.push_back(
        MakeKeyValue("malformed_events",
                     std::to_string(replay_result_.summary.malformed_events)));
    status.values.push_back(
        MakeKeyValue("truncated_records",
                     std::to_string(replay_result_.summary.truncated_records)));
    status.values.push_back(
        MakeKeyValue("publishable_actions", std::to_string(actions_.size())));
    status.values.push_back(
        MakeKeyValue("processed_actions", std::to_string(processed_actions_)));
    status.values.push_back(
        MakeKeyValue("runtime_actions", std::to_string(total_runtime_actions_)));
    status.values.push_back(
        MakeKeyValue("rtcm_actions", std::to_string(total_rtcm_actions_)));
    status.values.push_back(
        MakeKeyValue("rtcm_published_frames", std::to_string(rtcm_published_frames_)));
    status.values.push_back(
        MakeKeyValue("complete", replay_complete_ ? "true" : "false"));
    status.values.push_back(
        MakeKeyValue("wall_time_scale", std::to_string(config_.wall_time_scale)));
    status.values.push_back(
        MakeKeyValue("fallback_step_ms", std::to_string(config_.fallback_step_ms)));

    diagnostics.status.push_back(std::move(status));
  }

  void PublishDiagnosticsNow()
  {
    auto summary = BuildHealthSummary();
    last_diagnostics_message_ =
        ToDiagnosticArrayMessage(summary, "universal_gnss_replay", hardware_id_);

    if (last_diagnostics_message_->header.stamp.sec == 0 &&
        last_diagnostics_message_->header.stamp.nanosec == 0u)
    {
      const std::optional<universal_gnss::GnssTimestampNs> now_ns =
          owner_.now().nanoseconds();
      last_diagnostics_message_->header.stamp = ToRosTime(now_ns);
    }
    last_diagnostics_message_->header.frame_id = config_.frame_id;
    AppendProgressStatus(*last_diagnostics_message_);
    diagnostics_publisher_->publish(*last_diagnostics_message_);
  }

  void PublishRuntimeState(universal_gnss::GnssRuntimeState state)
  {
    if (!state.timestamp_ns.has_value())
    {
      state.timestamp_ns = owner_.now().nanoseconds();
    }

    current_state_ = state;
    has_runtime_state_ = true;

    last_status_message_ = ToGnssStatusMessage(current_state_);
    status_publisher_->publish(*last_status_message_);

    if (CanPublishFixMessage(current_state_))
    {
      last_fix_message_ = ToNavSatFixMessage(current_state_);
      last_fix_message_->header.frame_id = config_.frame_id;
      fix_publisher_->publish(*last_fix_message_);
    }
    else
    {
      last_fix_message_.reset();
    }
  }

  bool StepOnce()
  {
    if (replay_complete_ || next_action_index_ >= actions_.size())
    {
      replay_complete_ = true;
      if (timer_ != nullptr)
      {
        timer_->cancel();
      }
      PublishDiagnosticsNow();
      return false;
    }

    const std::size_t current_index = next_action_index_;
    const ReplayAction& action = actions_[current_index];
    ++next_action_index_;
    processed_actions_ = next_action_index_;

    if (action.state.has_value())
    {
      PublishRuntimeState(*action.state);
    }

    if (action.rtcm.has_value() && rtcm_publisher_ != nullptr)
    {
      last_rtcm_message_ = *action.rtcm;
      if ((last_rtcm_message_->stamp.sec == 0 && last_rtcm_message_->stamp.nanosec == 0u) &&
          action.timestamp_ns.has_value())
      {
        last_rtcm_message_->stamp = ToRosTime(action.timestamp_ns);
      }
      rtcm_publisher_->publish(*last_rtcm_message_);
      ++rtcm_published_frames_;
    }

    if (next_action_index_ >= actions_.size())
    {
      replay_complete_ = true;
      next_due_time_.reset();
      if (timer_ != nullptr)
      {
        timer_->cancel();
      }
    }
    else if (config_.replay_mode != ReplayMode::kStepped)
    {
      next_due_time_ = SteadyClock::now() + DelayToNextAction(current_index);
    }

    PublishDiagnosticsNow();
    return true;
  }

  ReplayNode& owner_;
  ReplayNodeConfig config_{};
  std::string hardware_id_{};
  std::vector<std::uint8_t> bytes_{};
  universal_gnss_tools::GnssReplayResult replay_result_{};
  std::vector<ReplayAction> actions_{};
  universal_gnss::GnssRuntimeState current_state_{};
  rclcpp::Publisher<universal_gnss_ros2::msg::GnssStatus>::SharedPtr status_publisher_{};
  rclcpp::Publisher<sensor_msgs::msg::NavSatFix>::SharedPtr fix_publisher_{};
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_publisher_{};
  rclcpp::Publisher<universal_gnss_ros2::msg::RtcmFrame>::SharedPtr rtcm_publisher_{};
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr step_service_{};
  rclcpp::TimerBase::SharedPtr timer_{};
  std::optional<SteadyClock::time_point> next_due_time_{};
  std::optional<sensor_msgs::msg::NavSatFix> last_fix_message_{};
  std::optional<universal_gnss_ros2::msg::GnssStatus> last_status_message_{};
  std::optional<diagnostic_msgs::msg::DiagnosticArray> last_diagnostics_message_{};
  std::optional<universal_gnss_ros2::msg::RtcmFrame> last_rtcm_message_{};
  std::size_t next_action_index_{0u};
  std::size_t processed_actions_{0u};
  std::size_t total_runtime_actions_{0u};
  std::size_t total_rtcm_actions_{0u};
  std::size_t rtcm_published_frames_{0u};
  bool has_runtime_state_{false};
  bool capture_timestamps_available_{false};
  bool load_succeeded_{true};
  bool replay_complete_{false};
};

ReplayNode::ReplayNode(const rclcpp::NodeOptions& options)
    : rclcpp::Node("universal_gnss_replay", options), impl_(std::make_unique<Impl>(*this))
{
}

ReplayNode::~ReplayNode() = default;

bool ReplayNode::StepOnce()
{
  return impl_->StepOnce();
}

bool ReplayNode::replay_complete() const
{
  return impl_->replay_complete_;
}

bool ReplayNode::publishers_ready() const
{
  return impl_->status_publisher_ != nullptr && impl_->fix_publisher_ != nullptr &&
         impl_->diagnostics_publisher_ != nullptr && impl_->step_service_ != nullptr &&
         (!impl_->config_.publish_rtcm || impl_->rtcm_publisher_ != nullptr);
}

bool ReplayNode::has_runtime_state() const
{
  return impl_->has_runtime_state_;
}

const universal_gnss::GnssRuntimeState& ReplayNode::current_state() const
{
  return impl_->current_state_;
}

const std::optional<sensor_msgs::msg::NavSatFix>& ReplayNode::last_fix_message() const
{
  return impl_->last_fix_message_;
}

const std::optional<universal_gnss_ros2::msg::GnssStatus>& ReplayNode::last_status_message() const
{
  return impl_->last_status_message_;
}

const std::optional<diagnostic_msgs::msg::DiagnosticArray>&
ReplayNode::last_diagnostics_message() const
{
  return impl_->last_diagnostics_message_;
}

const std::optional<universal_gnss_ros2::msg::RtcmFrame>& ReplayNode::last_rtcm_message() const
{
  return impl_->last_rtcm_message_;
}

}  // namespace universal_gnss_ros2
