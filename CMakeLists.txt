cmake_minimum_required(VERSION 3.16)

project(universal_gnss LANGUAGES CXX)

include(CTest)

option(UNIVERSAL_GNSS_BUILD_ROS2 "Build the universal_gnss_ros2 package" OFF)

add_subdirectory(gnss_core)
add_subdirectory(gnss_protocols)
add_subdirectory(gnss_driver)
add_subdirectory(gnss_transport)
add_subdirectory(gnss_runtime)
add_subdirectory(gnss_ntrip)
add_subdirectory(gnss_tools)

if(UNIVERSAL_GNSS_BUILD_ROS2)
  add_subdirectory(gnss_ros2)
endif()
