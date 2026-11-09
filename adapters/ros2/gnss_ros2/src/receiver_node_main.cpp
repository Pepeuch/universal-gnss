#include "universal_gnss_ros2/launch_incarnation.hpp"
#include "universal_gnss_ros2/receiver_node.hpp"

int main(int argc, char** argv)
{
  return universal_gnss_ros2::RunWithLaunchIncarnation<universal_gnss_ros2::ReceiverNode>(
      argc, argv, "source_incarnation");
}
