add_library(
  universal_gnss_tools
  src/config_apply.cpp
  src/config_plan.cpp
  src/rtcm_inspector.cpp
  src/gnss_stream_inspector.cpp
  src/gnss_replay.cpp
  src/gnss_quality_report.cpp
  src/ntrip_monitor.cpp
  src/profile_preview.cpp
  src/runtime_state_format.cpp
)

target_include_directories(
  universal_gnss_tools
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
  universal_gnss_tools
  PUBLIC
    universal_gnss_driver
    universal_gnss_ntrip
    universal_gnss_protocols
)

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

add_executable(
  gnss_quality_report
  src/gnss_quality_report_cli.cpp
)

target_link_libraries(
  gnss_quality_report
  PRIVATE
    universal_gnss_tools
)

add_executable(
  gnss_profile_preview
  src/gnss_profile_preview.cpp
)

target_link_libraries(
  gnss_profile_preview
  PRIVATE
    universal_gnss_tools
)

add_executable(
  gnss_config_plan
  src/gnss_config_plan.cpp
)

target_link_libraries(
  gnss_config_plan
  PRIVATE
    universal_gnss_tools
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(
    gnss_config_apply
    src/gnss_config_apply.cpp
  )
  target_link_libraries(
    gnss_config_apply
    PRIVATE
      universal_gnss_tools
      universal_gnss_transport
  )

  add_executable(
    gnss_serial_monitor
    src/gnss_serial_monitor.cpp
  )
  target_link_libraries(
    gnss_serial_monitor
    PRIVATE
      universal_gnss_driver
      universal_gnss_tools
      universal_gnss_transport
  )

  add_executable(
    gnss_ntrip_monitor
    src/gnss_ntrip_monitor.cpp
  )
  target_link_libraries(
    gnss_ntrip_monitor
    PRIVATE
      universal_gnss_ntrip
      universal_gnss_tools
  )
endif()

if(BUILD_TESTING)
  add_executable(
    gnss_tools_test_runtime_state_format
    tests/test_runtime_state_format.cpp
  )
  target_link_libraries(
    gnss_tools_test_runtime_state_format
    PRIVATE
      universal_gnss_tools
  )
  add_test(NAME gnss_tools_test_runtime_state_format COMMAND gnss_tools_test_runtime_state_format)

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

  add_executable(
    gnss_tools_test_gnss_quality_report
    tests/test_gnss_quality_report.cpp
  )
  target_link_libraries(
    gnss_tools_test_gnss_quality_report
    PRIVATE
      universal_gnss_tools
  )
  target_compile_definitions(
    gnss_tools_test_gnss_quality_report
    PRIVATE
      TESTDATA_DIR="${PROJECT_SOURCE_DIR}/testdata"
  )
  add_test(NAME gnss_tools_test_gnss_quality_report COMMAND gnss_tools_test_gnss_quality_report)

  add_executable(
    gnss_tools_test_profile_preview
    tests/test_profile_preview.cpp
  )
  target_link_libraries(
    gnss_tools_test_profile_preview
    PRIVATE
      universal_gnss_tools
  )
  add_test(NAME gnss_tools_test_profile_preview COMMAND gnss_tools_test_profile_preview)

  add_executable(
    gnss_tools_test_config_plan
    tests/test_config_plan.cpp
  )
  target_link_libraries(
    gnss_tools_test_config_plan
    PRIVATE
      universal_gnss_tools
  )
  add_test(NAME gnss_tools_test_config_plan COMMAND gnss_tools_test_config_plan)

  add_executable(
    gnss_tools_test_config_apply
    tests/test_config_apply.cpp
  )
  target_link_libraries(
    gnss_tools_test_config_apply
    PRIVATE
      universal_gnss_tools
      universal_gnss_driver
      universal_gnss_protocols
      universal_gnss_transport
  )
  add_test(NAME gnss_tools_test_config_apply COMMAND gnss_tools_test_config_apply)

  add_executable(
    gnss_tools_test_ntrip_monitor
    tests/test_ntrip_monitor.cpp
  )
  target_link_libraries(
    gnss_tools_test_ntrip_monitor
    PRIVATE
      universal_gnss_tools
  )
  add_test(NAME gnss_tools_test_ntrip_monitor COMMAND gnss_tools_test_ntrip_monitor)
endif()
