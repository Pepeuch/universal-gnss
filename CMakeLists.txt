cmake_minimum_required(VERSION 3.16)

project(universal_gnss LANGUAGES CXX)

include(CTest)

option(UNIVERSAL_GNSS_BUILD_ROS2 "Build the universal_gnss_ros2 package" OFF)

add_subdirectory(gnss_core)

if(UNIVERSAL_GNSS_BUILD_ROS2)
  add_subdirectory(gnss_ros2)
endif()
