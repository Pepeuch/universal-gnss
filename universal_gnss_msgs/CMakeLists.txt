cmake_minimum_required(VERSION 3.16)

project(universal_gnss_msgs LANGUAGES C CXX)

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(
  ${PROJECT_NAME}
  "msg/GnssStatus.msg"
  "msg/RtcmFrame.msg"
  "srv/GetReceiverSnapshot.srv"
  DEPENDENCIES builtin_interfaces diagnostic_msgs
)

if(BUILD_TESTING)
  find_package(ament_cmake_pytest REQUIRED)
  ament_add_pytest_test(
    test_interface_only
    tests/test_interface_only.py
    TIMEOUT 30
  )
endif()

ament_export_dependencies(
  builtin_interfaces
  diagnostic_msgs
  rosidl_default_runtime
)

ament_package()
