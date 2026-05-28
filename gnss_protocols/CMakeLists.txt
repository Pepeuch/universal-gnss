add_library(
  universal_gnss_protocols
  src/nmea_checksum.cpp
  src/nmea_parser.cpp
  src/rtcm_crc24q.cpp
  src/ubx_checksum.cpp
  src/ubx_parser.cpp
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

target_link_libraries(universal_gnss_protocols PUBLIC universal_gnss_core)

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

  add_executable(
    gnss_protocols_test_nmea_parser
    tests/test_nmea_parser.cpp
  )
  target_link_libraries(
    gnss_protocols_test_nmea_parser
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_nmea_parser COMMAND gnss_protocols_test_nmea_parser)

  add_executable(
    gnss_protocols_test_ubx_nav_pvt
    tests/test_ubx_nav_pvt.cpp
  )
  target_link_libraries(
    gnss_protocols_test_ubx_nav_pvt
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_ubx_nav_pvt COMMAND gnss_protocols_test_ubx_nav_pvt)

  add_executable(
    gnss_protocols_test_ubx_nav_sat
    tests/test_ubx_nav_sat.cpp
  )
  target_link_libraries(
    gnss_protocols_test_ubx_nav_sat
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_ubx_nav_sat COMMAND gnss_protocols_test_ubx_nav_sat)
endif()
