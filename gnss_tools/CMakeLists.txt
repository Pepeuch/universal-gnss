add_library(
  universal_gnss_tools
  src/rtcm_inspector.cpp
  src/gnss_stream_inspector.cpp
  src/gnss_replay.cpp
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

add_executable(
  gnss_inspect
  src/gnss_inspect.cpp
)

target_link_libraries(
  gnss_inspect
  PRIVATE
    universal_gnss_tools
)

add_executable(
  gnss_replay
  src/gnss_replay_cli.cpp
)

target_link_libraries(
  gnss_replay
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
  target_compile_definitions(
    gnss_tools_test_rtcm_inspector
    PRIVATE
      TESTDATA_DIR="${PROJECT_SOURCE_DIR}/testdata"
  )
  add_test(NAME gnss_tools_test_rtcm_inspector COMMAND gnss_tools_test_rtcm_inspector)

  add_executable(
    gnss_tools_test_gnss_stream_inspector
    tests/test_gnss_stream_inspector.cpp
  )
  target_link_libraries(
    gnss_tools_test_gnss_stream_inspector
    PRIVATE
      universal_gnss_tools
  )
  target_compile_definitions(
    gnss_tools_test_gnss_stream_inspector
    PRIVATE
      TESTDATA_DIR="${PROJECT_SOURCE_DIR}/testdata"
  )
  add_test(NAME gnss_tools_test_gnss_stream_inspector COMMAND gnss_tools_test_gnss_stream_inspector)

  add_executable(
    gnss_tools_test_gnss_replay
    tests/test_gnss_replay.cpp
  )
  target_link_libraries(
    gnss_tools_test_gnss_replay
    PRIVATE
      universal_gnss_tools
  )
  target_compile_definitions(
    gnss_tools_test_gnss_replay
    PRIVATE
      TESTDATA_DIR="${PROJECT_SOURCE_DIR}/testdata"
  )
  add_test(NAME gnss_tools_test_gnss_replay COMMAND gnss_tools_test_gnss_replay)
endif()
