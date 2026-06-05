#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "universal_gnss_ros2/replay_node.hpp"

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<universal_gnss_ros2::ReplayNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
