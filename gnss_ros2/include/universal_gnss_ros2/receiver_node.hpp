#pragma once

#include <memory>
#include <optional>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "rclcpp/node.hpp"
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"

namespace universal_gnss_transport
{

class ByteSource;

}  // namespace universal_gnss_transport

namespace universal_gnss_ros2
{

class ReceiverNode : public rclcpp::Node
{
public:
  explicit ReceiverNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});
  ReceiverNode(std::unique_ptr<universal_gnss_transport::ByteSource> source,
               const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});
  ~ReceiverNode() override;

  bool StepOnce();
  void PublishNow();

  bool has_transport_source() const;
  bool publishers_ready() const;

  const universal_gnss::GnssRuntimeState& current_state() const;
  const std::optional<sensor_msgs::msg::NavSatFix>& last_fix_message() const;
  const std::optional<universal_gnss_ros2::msg::GnssStatus>& last_status_message() const;
  const std::optional<diagnostic_msgs::msg::DiagnosticArray>& last_diagnostics_message() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace universal_gnss_ros2
