cmake_minimum_required(VERSION 3.16)

project(universal_gnss_ros2 LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(rosidl_default_runtime REQUIRED)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
endif()

set(UNIVERSAL_GNSS_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gnss_core/include")

if(NOT EXISTS "${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}/universal_gnss/gnss_runtime_state.hpp")
  message(FATAL_ERROR "gnss_core headers were not found next to gnss_ros2")
endif()

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

install(
  TARGETS ${PROJECT_NAME}_gnss_status_adapter
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY "${UNIVERSAL_GNSS_CORE_INCLUDE_DIR}/" DESTINATION include)

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
endif()

ament_export_include_directories(include)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(builtin_interfaces rosidl_default_runtime)

ament_package()
