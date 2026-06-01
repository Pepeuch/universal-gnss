#pragma once

#include <memory>
#include <optional>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "rclcpp/node.hpp"

namespace universal_gnss_ros2
{

class NtripNode : public rclcpp::Node
{
public:
  explicit NtripNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});
  NtripNode(int adopted_socket_fd, const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});
  ~NtripNode() override;

  bool StepOnce();
  void PublishNow();

  bool client_ready() const;
  bool diagnostics_ready() const;
  bool has_runtime_state() const;

  const std::optional<diagnostic_msgs::msg::DiagnosticArray>& last_diagnostics_message() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace universal_gnss_ros2
