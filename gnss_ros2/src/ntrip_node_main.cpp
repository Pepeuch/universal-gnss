#include "universal_gnss_ros2/launch_incarnation.hpp"
#include "universal_gnss_ros2/ntrip_node.hpp"

int main(int argc, char** argv)
{
  return universal_gnss_ros2::RunWithLaunchIncarnation<universal_gnss_ros2::NtripNode>(
      argc, argv, "expected_source_incarnation");
}
