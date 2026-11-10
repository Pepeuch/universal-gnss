cmake_minimum_required(VERSION 3.22)
project(ug_installed_consumer LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(universal_gnss_msgs REQUIRED)
find_package(universal_gnss_ros2 REQUIRED)
find_package(universal_gnss_mavros REQUIRED)
find_package(mavros 2.16.0 EXACT REQUIRED)
find_package(pluginlib REQUIRED)
find_package(diagnostic_updater REQUIRED)
find_package(libmavconn REQUIRED)
find_package(mavlink REQUIRED)
find_package(mavros_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcpputils REQUIRED)
find_package(tf2_eigen REQUIRED)
find_package(tf2_ros REQUIRED)

add_executable(installed_consumer consumer.cpp)
target_compile_features(installed_consumer PRIVATE cxx_std_17)
target_include_directories(installed_consumer PRIVATE
  ${mavros_INCLUDE_DIRS} ${libmavconn_INCLUDE_DIRS} ${mavlink_INCLUDE_DIRS})
target_link_libraries(installed_consumer PRIVATE
  universal_gnss_ros2::universal_gnss_ros2_gnss_status_adapter
  universal_gnss_ros2::universal_gnss_ros2_navsat_fix_adapter
  universal_gnss_mavros::universal_gnss_mavros_rtcm_encoder
  ${universal_gnss_msgs_TARGETS}
  pluginlib::pluginlib rclcpp::rclcpp rcpputils::rcpputils
  diagnostic_updater::diagnostic_updater
  tf2_eigen::tf2_eigen tf2_ros::tf2_ros tf2_ros::static_transform_broadcaster_node
  ${mavros_LIBRARIES} ${libmavconn_LIBRARIES} ${mavros_msgs_TARGETS} ${sensor_msgs_TARGETS})
