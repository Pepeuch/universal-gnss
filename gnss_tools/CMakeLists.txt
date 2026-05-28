add_library(
  universal_gnss_tools
  src/rtcm_inspector.cpp
)

target_include_directories(
  universal_gnss_tools
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(universal_gnss_tools PUBLIC universal_gnss_protocols)

target_compile_features(universal_gnss_tools PUBLIC cxx_std_17)

add_executable(
  rtcm_inspect
  src/rtcm_inspect.cpp
)

target_link_libraries(
  rtcm_inspect
  PRIVATE
    universal_gnss_tools
)

if(BUILD_TESTING)
  add_executable(
    gnss_tools_test_rtcm_inspector
    tests/test_rtcm_inspector.cpp
  )
  target_link_libraries(
    gnss_tools_test_rtcm_inspector
    PRIVATE
      universal_gnss_tools
  )
  add_test(NAME gnss_tools_test_rtcm_inspector COMMAND gnss_tools_test_rtcm_inspector)
endif()
