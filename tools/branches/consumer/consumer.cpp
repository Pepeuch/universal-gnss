#include <filesystem>
#include <iostream>
#include <stdexcept>

#include <mavros/mavros_uas.hpp>
#include <mavros/plugin.hpp>
#include <pluginlib/class_loader.hpp>
#include <universal_gnss_mavros/mavlink_rtcm_encoder.hpp>
#include <universal_gnss_msgs/srv/get_receiver_snapshot.hpp>
#include <universal_gnss_ros2/gnss_status_adapter.hpp>
#include <universal_gnss_ros2/navsat_fix_adapter.hpp>

int main(int argc, char** argv)
try
{
  if (argc != 2)
  {
    throw std::runtime_error("expected relocated MAVROS prefix");
  }
  universal_gnss::GnssRuntimeState state;
  const auto status = universal_gnss_ros2::ToGnssStatusMessage(state);
  const auto restored = universal_gnss_ros2::FromGnssStatusMessage(status);
  const auto fix = universal_gnss_ros2::ToNavSatFixMessage(restored);
  (void)fix;

  pluginlib::ClassLoader<mavros::plugin::PluginFactory> loader("mavros",
                                                               "mavros::plugin::PluginFactory");
  const auto factory = loader.createSharedInstance("universal_gnss");
  const auto library = std::filesystem::canonical(loader.getClassLibraryPath("universal_gnss"));
  const auto expected = std::filesystem::canonical(std::filesystem::path(argv[1]) /
                                                   "lib/libuniversal_gnss_mavros_plugin.so");
  if (!factory || library != expected)
  {
    throw std::runtime_error("factory did not load the relocated installed library");
  }
  std::cout << "PASS installed C++ exports and FACTORY_LOADED " << library << std::endl;
}
catch (const std::exception& error)
{
  std::cerr << "FAIL installed consumer: " << error.what() << std::endl;
  return 1;
}
