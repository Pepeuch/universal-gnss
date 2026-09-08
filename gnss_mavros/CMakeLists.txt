cmake_minimum_required(VERSION 3.16)

project(universal_gnss_mavros LANGUAGES CXX)

set(_universal_gnss_mavros_standalone OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_universal_gnss_mavros_standalone ON)
  find_package(ament_cmake REQUIRED)
  find_package(universal_gnss_ros2 REQUIRED)
endif()

add_library(universal_gnss_mavros_adapter src/mavlink_gnss_adapter.cpp)
target_compile_features(universal_gnss_mavros_adapter PUBLIC cxx_std_17)
set_target_properties(universal_gnss_mavros_adapter PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(
  universal_gnss_mavros_adapter
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
if(TARGET universal_gnss_core)
  target_link_libraries(universal_gnss_mavros_adapter PUBLIC universal_gnss_core)
else()
  target_include_directories(
    universal_gnss_mavros_adapter
    PUBLIC ${universal_gnss_ros2_INCLUDE_DIRS}
  )
endif()

if(_universal_gnss_mavros_standalone)
  option(UNIVERSAL_GNSS_BUILD_MAVROS_PLUGIN "Build the external MAVROS plugin" ON)
endif()

if(UNIVERSAL_GNSS_BUILD_MAVROS_PLUGIN)
  if(NOT _universal_gnss_mavros_standalone)
    find_package(ament_cmake REQUIRED)
    find_package(universal_gnss_ros2 REQUIRED)
  endif()
  find_package(diagnostic_msgs REQUIRED)
  find_package(diagnostic_updater REQUIRED)
  find_package(eigen3_cmake_module REQUIRED)
  find_package(Eigen3 REQUIRED)
  set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};/usr/share/cmake/geographiclib")
  find_package(GeographicLib REQUIRED)
  find_package(libmavconn REQUIRED)
  find_package(mavlink REQUIRED)
  find_package(mavros_msgs REQUIRED)
  find_package(mavros 2.15.1 EXACT REQUIRED)
  find_package(pluginlib REQUIRED)
  find_package(rclcpp REQUIRED)
  find_package(rclcpp_components REQUIRED)
  find_package(rcpputils REQUIRED)
  find_package(sensor_msgs REQUIRED)
  find_package(tf2_eigen REQUIRED)
  find_package(tf2_ros REQUIRED)

  add_library(universal_gnss_mavros_plugin SHARED src/universal_gnss_plugin.cpp)
  target_compile_features(universal_gnss_mavros_plugin PUBLIC cxx_std_17)
  target_include_directories(
    universal_gnss_mavros_plugin
    PRIVATE
      ${GeographicLib_INCLUDE_DIRS}
      ${libmavconn_INCLUDE_DIRS}
      ${mavlink_INCLUDE_DIRS}
      ${mavros_INCLUDE_DIRS}
      ${universal_gnss_ros2_INCLUDE_DIRS}
  )
  target_link_libraries(
    universal_gnss_mavros_plugin
    universal_gnss_mavros_adapter
    ${GeographicLib_LIBRARIES}
    ${libmavconn_LIBRARIES}
    ${mavros_LIBRARIES}
    ${mavros_msgs_TARGETS}
    ${sensor_msgs_TARGETS}
    ${universal_gnss_ros2_TARGETS}
    diagnostic_updater::diagnostic_updater
    pluginlib::pluginlib
    rclcpp::rclcpp
    rclcpp_components::component
    rcpputils::rcpputils
    sensor_msgs::sensor_msgs_library
    tf2_eigen::tf2_eigen
    tf2_ros::static_transform_broadcaster_node
    tf2_ros::tf2_ros
    universal_gnss_ros2::universal_gnss_ros2_gnss_status_adapter
    universal_gnss_ros2::universal_gnss_ros2_navsat_fix_adapter
  )
  pluginlib_export_plugin_description_file(mavros universal_gnss_mavros_plugins.xml)

  install(
    TARGETS universal_gnss_mavros_plugin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
  )
endif()

install(
  TARGETS universal_gnss_mavros_adapter
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  add_executable(gnss_mavros_test_adapter tests/test_mavlink_gnss_adapter.cpp)
  target_link_libraries(gnss_mavros_test_adapter PRIVATE universal_gnss_mavros_adapter)
  add_test(NAME gnss_mavros_test_adapter COMMAND gnss_mavros_test_adapter)
endif()

if(_universal_gnss_mavros_standalone)
  ament_export_include_directories(include)
  ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
  ament_export_dependencies(universal_gnss_ros2)
  ament_package()
endif()
