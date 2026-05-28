add_library(
  universal_gnss_driver
  src/receiver_profiles.cpp
  src/stream_detector.cpp
  src/unicore_session.cpp
)

target_include_directories(
  universal_gnss_driver
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(universal_gnss_driver PUBLIC universal_gnss_protocols)

target_compile_features(universal_gnss_driver PUBLIC cxx_std_17)

if(BUILD_TESTING)
  add_executable(
    gnss_driver_test_foundation
    tests/test_driver_foundation.cpp
  )
  target_link_libraries(
    gnss_driver_test_foundation
    PRIVATE
      universal_gnss_driver
  )
  add_test(NAME gnss_driver_test_foundation COMMAND gnss_driver_test_foundation)

  add_executable(
    gnss_driver_test_unicore_session
    tests/test_unicore_session.cpp
  )
  target_link_libraries(
    gnss_driver_test_unicore_session
    PRIVATE
      universal_gnss_driver
  )
  add_test(NAME gnss_driver_test_unicore_session COMMAND gnss_driver_test_unicore_session)
endif()
