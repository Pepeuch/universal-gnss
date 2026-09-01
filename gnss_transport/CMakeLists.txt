add_library(
  universal_gnss_transport
  src/memory_stream.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(OpenSSL REQUIRED)
  target_sources(
    universal_gnss_transport
    PRIVATE
      src/posix_serial_transport.cpp
      src/tcp_client_transport.cpp
  )
  target_compile_definitions(
    universal_gnss_transport
    PUBLIC
      UNIVERSAL_GNSS_TRANSPORT_HAS_POSIX_SERIAL=1
      UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT=1
  )
  target_link_libraries(universal_gnss_transport PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

target_include_directories(
  universal_gnss_transport
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(universal_gnss_transport PUBLIC cxx_std_17)

if(BUILD_TESTING)
  add_executable(
    gnss_transport_test_foundation
    tests/test_transport_foundation.cpp
  )
  target_link_libraries(
    gnss_transport_test_foundation
    PRIVATE
      universal_gnss_transport
  )
  add_test(NAME gnss_transport_test_foundation COMMAND gnss_transport_test_foundation)

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(
      gnss_transport_test_posix_serial_transport
      tests/test_posix_serial_transport.cpp
    )
    target_link_libraries(
      gnss_transport_test_posix_serial_transport
      PRIVATE
        universal_gnss_transport
    )
    add_test(
      NAME gnss_transport_test_posix_serial_transport
      COMMAND gnss_transport_test_posix_serial_transport
    )

    add_executable(
      gnss_transport_test_tcp_client_transport
      tests/test_tcp_client_transport.cpp
    )
    target_link_libraries(
      gnss_transport_test_tcp_client_transport
      PRIVATE
        universal_gnss_transport
    )
    add_test(
      NAME gnss_transport_test_tcp_client_transport
      COMMAND gnss_transport_test_tcp_client_transport
    )
  endif()
endif()
