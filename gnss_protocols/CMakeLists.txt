add_library(
  universal_gnss_protocols
  src/nmea_checksum.cpp
  src/nmea_parser.cpp
  src/rtcm_correction_monitor.cpp
  src/rtcm_crc24q.cpp
  src/rtcm_parser.cpp
  src/ubx_checksum.cpp
  src/ubx_cfg_builder.cpp
  src/ubx_parser.cpp
  src/unicore_parser.cpp
  src/unicore_binary_framer.cpp
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
    gnss_protocols_test_rtcm_parser
    tests/test_rtcm_parser.cpp
  )
  target_link_libraries(
    gnss_protocols_test_rtcm_parser
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_rtcm_parser COMMAND gnss_protocols_test_rtcm_parser)

  add_executable(
    gnss_protocols_test_rtcm_base_position
    tests/test_rtcm_base_position.cpp
  )
  target_link_libraries(
    gnss_protocols_test_rtcm_base_position
    PRIVATE
      universal_gnss_protocols
  )
  add_test(
    NAME gnss_protocols_test_rtcm_base_position
    COMMAND gnss_protocols_test_rtcm_base_position
  )

  add_executable(
    gnss_protocols_test_rtcm_correction_monitor
    tests/test_rtcm_correction_monitor.cpp
  )
  target_link_libraries(
    gnss_protocols_test_rtcm_correction_monitor
    PRIVATE
      universal_gnss_protocols
  )
  add_test(
    NAME gnss_protocols_test_rtcm_correction_monitor
    COMMAND gnss_protocols_test_rtcm_correction_monitor
  )

  add_executable(
    gnss_protocols_test_ubx_cfg_builder
    tests/test_ubx_cfg_builder.cpp
  )
  target_link_libraries(
    gnss_protocols_test_ubx_cfg_builder
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_ubx_cfg_builder COMMAND gnss_protocols_test_ubx_cfg_builder)

  add_executable(
    gnss_protocols_test_ubx_ack
    tests/test_ubx_ack.cpp
  )
  target_link_libraries(
    gnss_protocols_test_ubx_ack
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_ubx_ack COMMAND gnss_protocols_test_ubx_ack)

  add_executable(
    gnss_protocols_test_ubx_nav_status
    tests/test_ubx_nav_status.cpp
  )
  target_link_libraries(
    gnss_protocols_test_ubx_nav_status
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_ubx_nav_status COMMAND gnss_protocols_test_ubx_nav_status)

  add_executable(
    gnss_protocols_test_ubx_rxm_rtcm
    tests/test_ubx_rxm_rtcm.cpp
  )
  target_link_libraries(
    gnss_protocols_test_ubx_rxm_rtcm
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_ubx_rxm_rtcm COMMAND gnss_protocols_test_ubx_rxm_rtcm)

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
    gnss_protocols_test_ubx_nav_dop
    tests/test_ubx_nav_dop.cpp
  )
  target_link_libraries(
    gnss_protocols_test_ubx_nav_dop
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_ubx_nav_dop COMMAND gnss_protocols_test_ubx_nav_dop)

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

  add_executable(
    gnss_protocols_test_ubx_mon_rf
    tests/test_ubx_mon_rf.cpp
  )
  target_link_libraries(
    gnss_protocols_test_ubx_mon_rf
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_ubx_mon_rf COMMAND gnss_protocols_test_ubx_mon_rf)

  add_executable(
    gnss_protocols_test_unicore_ascii_parser
    tests/test_unicore_ascii_parser.cpp
  )
  target_link_libraries(
    gnss_protocols_test_unicore_ascii_parser
    PRIVATE
      universal_gnss_protocols
  )
  add_test(NAME gnss_protocols_test_unicore_ascii_parser COMMAND gnss_protocols_test_unicore_ascii_parser)

  add_executable(
    gnss_protocols_test_unicore_binary_framer
    tests/test_unicore_binary_framer.cpp
  )
  target_link_libraries(
    gnss_protocols_test_unicore_binary_framer
    PRIVATE
      universal_gnss_protocols
  )
  add_test(
    NAME gnss_protocols_test_unicore_binary_framer
    COMMAND gnss_protocols_test_unicore_binary_framer
  )

  add_executable(
    gnss_protocols_test_unicore_binary_bestnav
    tests/test_unicore_binary_bestnav.cpp
  )
  target_link_libraries(
    gnss_protocols_test_unicore_binary_bestnav
    PRIVATE
      universal_gnss_protocols
  )
  add_test(
    NAME gnss_protocols_test_unicore_binary_bestnav
    COMMAND gnss_protocols_test_unicore_binary_bestnav
  )

  add_executable(
    gnss_protocols_test_unicore_binary_pvtsln
    tests/test_unicore_binary_pvtsln.cpp
  )
  target_link_libraries(
    gnss_protocols_test_unicore_binary_pvtsln
    PRIVATE
      universal_gnss_protocols
  )
  add_test(
    NAME gnss_protocols_test_unicore_binary_pvtsln
    COMMAND gnss_protocols_test_unicore_binary_pvtsln
  )
endif()
