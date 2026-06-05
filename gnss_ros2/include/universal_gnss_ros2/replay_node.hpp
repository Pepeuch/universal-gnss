#pragma once

#include <memory>
#include <optional>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "rclcpp/node.hpp"
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"
#include "universal_gnss_ros2/msg/rtcm_frame.hpp"

namespace universal_gnss_ros2
{

class ReplayNode : public rclcpp::Node
{
public:
  explicit ReplayNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});
  ~ReplayNode() override;

  bool StepOnce();
  bool replay_complete() const;
  bool publishers_ready() const;
  bool has_runtime_state() const;

  const universal_gnss::GnssRuntimeState& current_state() const;
  const std::optional<sensor_msgs::msg::NavSatFix>& last_fix_message() const;
  const std::optional<universal_gnss_ros2::msg::GnssStatus>& last_status_message() const;
  const std::optional<diagnostic_msgs::msg::DiagnosticArray>& last_diagnostics_message() const;
  const std::optional<universal_gnss_ros2::msg::RtcmFrame>& last_rtcm_message() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace universal_gnss_ros2
