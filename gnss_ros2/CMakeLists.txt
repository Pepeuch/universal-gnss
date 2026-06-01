cmake_minimum_required(VERSION 3.16)

project(universal_gnss_ros2 LANGUAGES C CXX)

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(rosidl_default_runtime REQUIRED)
find_package(sensor_msgs REQUIRED)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
endif()

set(UNIVERSAL_GNSS_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gnss_core/include")

if(NOT EXISTS "${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}/universal_gnss/gnss_runtime_state.hpp")
  message(FATAL_ERROR "gnss_core headers were not found next to gnss_ros2")
endif()

set(_saved_build_testing "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
if(NOT TARGET universal_gnss_core)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../gnss_core" "${CMAKE_CURRENT_BINARY_DIR}/gnss_core")
endif()
if(NOT TARGET universal_gnss_protocols)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../gnss_protocols"
    "${CMAKE_CURRENT_BINARY_DIR}/gnss_protocols"
  )
endif()
if(NOT TARGET universal_gnss_transport)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../gnss_transport"
    "${CMAKE_CURRENT_BINARY_DIR}/gnss_transport"
  )
endif()
if(NOT TARGET universal_gnss_driver)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../gnss_driver"
    "${CMAKE_CURRENT_BINARY_DIR}/gnss_driver"
  )
endif()
if(NOT TARGET universal_gnss_ntrip)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../gnss_ntrip"
    "${CMAKE_CURRENT_BINARY_DIR}/gnss_ntrip"
  )
endif()
set(BUILD_TESTING "${_saved_build_testing}")

rosidl_generate_interfaces(
  ${PROJECT_NAME}
  "msg/GnssStatus.msg"
  DEPENDENCIES builtin_interfaces
)

add_library(${PROJECT_NAME}_gnss_status_adapter src/gnss_status_adapter.cpp)
target_compile_features(${PROJECT_NAME}_gnss_status_adapter PUBLIC cxx_std_17)
target_include_directories(
  ${PROJECT_NAME}_gnss_status_adapter
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(${PROJECT_NAME}_gnss_status_adapter builtin_interfaces)

rosidl_get_typesupport_target(
  cpp_typesupport_target
  ${PROJECT_NAME}
  "rosidl_typesupport_cpp"
)
target_link_libraries(${PROJECT_NAME}_gnss_status_adapter "${cpp_typesupport_target}")

add_library(${PROJECT_NAME}_navsat_fix_adapter src/navsat_fix_adapter.cpp)
target_compile_features(${PROJECT_NAME}_navsat_fix_adapter PUBLIC cxx_std_17)
target_include_directories(
  ${PROJECT_NAME}_navsat_fix_adapter
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(${PROJECT_NAME}_navsat_fix_adapter sensor_msgs)
target_link_libraries(
  ${PROJECT_NAME}_navsat_fix_adapter
  ${PROJECT_NAME}_gnss_status_adapter
)

add_library(${PROJECT_NAME}_diagnostic_adapter src/diagnostic_adapter.cpp)
target_compile_features(${PROJECT_NAME}_diagnostic_adapter PUBLIC cxx_std_17)
target_include_directories(
  ${PROJECT_NAME}_diagnostic_adapter
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(${PROJECT_NAME}_diagnostic_adapter diagnostic_msgs)
target_link_libraries(
  ${PROJECT_NAME}_diagnostic_adapter
  ${PROJECT_NAME}_gnss_status_adapter
)

add_library(${PROJECT_NAME}_receiver_node STATIC src/receiver_node.cpp)
target_compile_features(${PROJECT_NAME}_receiver_node PUBLIC cxx_std_17)
target_include_directories(
  ${PROJECT_NAME}_receiver_node
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(
  ${PROJECT_NAME}_receiver_node
  diagnostic_msgs
  rclcpp
  sensor_msgs
)
target_link_libraries(
  ${PROJECT_NAME}_receiver_node
  ${PROJECT_NAME}_diagnostic_adapter
  ${PROJECT_NAME}_gnss_status_adapter
  ${PROJECT_NAME}_navsat_fix_adapter
  universal_gnss_driver
  universal_gnss_transport
  "${cpp_typesupport_target}"
)

add_executable(receiver_node src/receiver_node_main.cpp)
target_link_libraries(
  receiver_node
  ${PROJECT_NAME}_receiver_node
)
ament_target_dependencies(receiver_node rclcpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(${PROJECT_NAME}_ntrip_node STATIC src/ntrip_node.cpp)
  target_compile_features(${PROJECT_NAME}_ntrip_node PUBLIC cxx_std_17)
  target_include_directories(
    ${PROJECT_NAME}_ntrip_node
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}>
      $<INSTALL_INTERFACE:include>
  )
  ament_target_dependencies(
    ${PROJECT_NAME}_ntrip_node
    diagnostic_msgs
    rclcpp
  )
  target_link_libraries(
    ${PROJECT_NAME}_ntrip_node
    ${PROJECT_NAME}_diagnostic_adapter
    ${PROJECT_NAME}_gnss_status_adapter
    universal_gnss_ntrip
    "${cpp_typesupport_target}"
  )

  add_executable(ntrip_node src/ntrip_node_main.cpp)
  target_link_libraries(
    ntrip_node
    ${PROJECT_NAME}_ntrip_node
  )
  ament_target_dependencies(ntrip_node rclcpp)
endif()

install(
  TARGETS
    ${PROJECT_NAME}_gnss_status_adapter
    ${PROJECT_NAME}_navsat_fix_adapter
    ${PROJECT_NAME}_diagnostic_adapter
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  TARGETS
    ${PROJECT_NAME}_receiver_node
    receiver_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  install(
    TARGETS
      ${PROJECT_NAME}_ntrip_node
      ntrip_node
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION lib/${PROJECT_NAME}
  )
endif()

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY "${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}/" DESTINATION include)
install(
  DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
  PATTERN "__pycache__" EXCLUDE
  PATTERN "*.pyc" EXCLUDE
)

if(BUILD_TESTING)
  ament_add_gtest(test_gnss_status_adapter tests/test_gnss_status_adapter.cpp)
  if(TARGET test_gnss_status_adapter)
    target_link_libraries(
      test_gnss_status_adapter
      ${PROJECT_NAME}_gnss_status_adapter
      "${cpp_typesupport_target}"
    )
    target_include_directories(
      test_gnss_status_adapter
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}
    )
  endif()

  ament_add_gtest(test_navsat_fix_adapter tests/test_navsat_fix_adapter.cpp)
  if(TARGET test_navsat_fix_adapter)
    target_link_libraries(
      test_navsat_fix_adapter
      ${PROJECT_NAME}_navsat_fix_adapter
    )
    target_include_directories(
      test_navsat_fix_adapter
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}
    )
  endif()

  ament_add_gtest(test_diagnostic_adapter tests/test_diagnostic_adapter.cpp)
  if(TARGET test_diagnostic_adapter)
    target_link_libraries(
      test_diagnostic_adapter
      ${PROJECT_NAME}_diagnostic_adapter
    )
    target_include_directories(
      test_diagnostic_adapter
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}
    )
  endif()

  ament_add_gtest(test_receiver_node tests/test_receiver_node.cpp)
  if(TARGET test_receiver_node)
    target_link_libraries(
      test_receiver_node
      ${PROJECT_NAME}_receiver_node
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_link_libraries(
        test_receiver_node
        ${PROJECT_NAME}_ntrip_node
      )
    endif()
    target_include_directories(
      test_receiver_node
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}
    )
    ament_target_dependencies(
      test_receiver_node
      rclcpp
    )
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    ament_add_gtest(test_ntrip_node tests/test_ntrip_node.cpp)
    if(TARGET test_ntrip_node)
      target_link_libraries(
        test_ntrip_node
        ${PROJECT_NAME}_ntrip_node
      )
      target_include_directories(
        test_ntrip_node
        PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/include
          ${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}
      )
      ament_target_dependencies(
        test_ntrip_node
        rclcpp
      )
    endif()
  endif()
endif()

ament_export_include_directories(include)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(
  builtin_interfaces
  diagnostic_msgs
  rclcpp
  rosidl_default_runtime
  sensor_msgs
)

ament_package()
