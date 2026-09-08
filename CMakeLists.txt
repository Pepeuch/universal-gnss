cmake_minimum_required(VERSION 3.16)

project(universal_gnss LANGUAGES CXX)

include(CTest)

option(UNIVERSAL_GNSS_BUILD_ROS2 "Build the universal_gnss_ros2 package" OFF)
option(UNIVERSAL_GNSS_BUILD_MAVROS_PLUGIN "Build the optional external MAVROS plugin" OFF)

add_subdirectory(gnss_core)
add_subdirectory(gnss_protocols)
add_subdirectory(gnss_driver)
add_subdirectory(gnss_transport)
add_subdirectory(gnss_ntrip)
add_subdirectory(gnss_runtime)
add_subdirectory(gnss_tools)
add_subdirectory(gnss_mavros)

if(UNIVERSAL_GNSS_BUILD_ROS2)
  add_subdirectory(gnss_ros2)
endif()
