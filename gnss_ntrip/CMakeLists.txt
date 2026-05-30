add_library(
  universal_gnss_ntrip
  src/gga_generator.cpp
  src/gga_sentence_builder.cpp
  src/ntrip_request.cpp
  src/ntrip_client.cpp
  src/ntrip_reconnect_policy.cpp
)

target_include_directories(
  universal_gnss_ntrip
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(universal_gnss_ntrip PUBLIC cxx_std_17)

target_link_libraries(
  universal_gnss_ntrip
  PUBLIC
    universal_gnss_protocols
    universal_gnss_transport
)

if(BUILD_TESTING)
  add_executable(
    gnss_ntrip_test_foundation
    tests/test_ntrip_foundation.cpp
  )
  target_link_libraries(
    gnss_ntrip_test_foundation
    PRIVATE
      universal_gnss_ntrip
  )
  add_test(NAME gnss_ntrip_test_foundation COMMAND gnss_ntrip_test_foundation)

  add_executable(
    gnss_ntrip_test_ntrip_client
    tests/test_ntrip_client.cpp
  )
  target_link_libraries(
    gnss_ntrip_test_ntrip_client
    PRIVATE
      universal_gnss_ntrip
  )
  add_test(NAME gnss_ntrip_test_ntrip_client COMMAND gnss_ntrip_test_ntrip_client)

  add_executable(
    gnss_ntrip_test_ntrip_reconnect_policy
    tests/test_ntrip_reconnect_policy.cpp
  )
  target_link_libraries(
    gnss_ntrip_test_ntrip_reconnect_policy
    PRIVATE
      universal_gnss_ntrip
  )
  add_test(
    NAME gnss_ntrip_test_ntrip_reconnect_policy
    COMMAND gnss_ntrip_test_ntrip_reconnect_policy
  )

  add_executable(
    gnss_ntrip_test_gga_generator
    tests/test_gga_generator.cpp
  )
  target_link_libraries(
    gnss_ntrip_test_gga_generator
    PRIVATE
      universal_gnss_ntrip
  )
  add_test(NAME gnss_ntrip_test_gga_generator COMMAND gnss_ntrip_test_gga_generator)

  add_executable(
    gnss_ntrip_test_gga_sentence_builder
    tests/test_gga_sentence_builder.cpp
  )
  target_link_libraries(
    gnss_ntrip_test_gga_sentence_builder
    PRIVATE
      universal_gnss_ntrip
  )
  add_test(
    NAME gnss_ntrip_test_gga_sentence_builder
    COMMAND gnss_ntrip_test_gga_sentence_builder
  )
endif()
