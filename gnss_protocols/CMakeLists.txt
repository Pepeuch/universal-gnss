add_library(
  universal_gnss_protocols
  src/nmea_checksum.cpp
  src/rtcm_crc24q.cpp
  src/ubx_checksum.cpp
  src/nmea_framer.cpp
  src/rtcm_framer.cpp
  src/ubx_framer.cpp
  src/unicore_framer.cpp
)

target_include_directories(
  universal_gnss_protocols
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(universal_gnss_protocols PUBLIC cxx_std_17)

if(BUILD_TESTING)
  add_executable(
    gnss_protocols_test_foundation
    tests/test_protocol_foundation.cpp
  )
  target_link_libraries(
    gnss_protocols_test_foundation
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_foundation COMMAND gnss_protocols_test_foundation)
endif()
